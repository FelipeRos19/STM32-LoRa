/**
  ******************************************************************************
  * @file    proto_frame.h
  * @brief   Serial framing: SOF(0x7E) | type | len | payload[len] | crc16 (BE).
  *          The CRC covers type, len and payload.
  *
  *          The decoder is a byte-wise state machine. When a frame fails
  *          (bad CRC or invalid length) it rescans the bytes that followed
  *          the failed SOF, so a real frame hidden behind a spurious 0x7E is
  *          not lost.
  ******************************************************************************
  */

#ifndef PROTO_FRAME_H
#define PROTO_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_SOF              0x7EU
#define PROTO_MAX_PAYLOAD      250U
#define PROTO_OVERHEAD         5U    /* SOF + type + len + crc16 */
#define PROTO_MAX_FRAME        (PROTO_MAX_PAYLOAD + PROTO_OVERHEAD)
#define PROTO_BYTE_TIMEOUT_MS  100U  /* gap inside a frame that drops it */

typedef enum
{
  PROTO_EVT_FRAME = 0,   /* complete, CRC-checked frame */
  PROTO_EVT_ERR_CRC,     /* CRC mismatch */
  PROTO_EVT_ERR_LEN,     /* len above PROTO_MAX_PAYLOAD */
  PROTO_EVT_TIMEOUT,     /* partial frame dropped after an inter-byte gap */
} proto_evt_t;

/* Called for every decoded frame or error. For errors, type/payload are not meaningful. */
typedef void (*proto_frame_cb_t)(void *ctx, proto_evt_t evt, uint8_t type,
                                 const uint8_t *payload, uint8_t len);

typedef struct
{
  uint8_t state;
  uint8_t type;
  uint8_t len;
  uint16_t crc_rx;
  uint16_t raw_len;                 /* bytes of the current candidate, SOF included */
  uint32_t last_byte_ms;
  uint8_t raw[PROTO_MAX_FRAME];     /* current candidate frame */
  uint8_t work[PROTO_MAX_FRAME];    /* rescan buffer after an error */
} proto_decoder_t;

/* Encodes one frame into out. Returns the frame size, or 0 if it does not fit. */
size_t proto_frame_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t out_size);

void proto_decoder_init(proto_decoder_t *dec);

/* Feeds received bytes. now_ms is any monotonic millisecond clock. */
void proto_decoder_feed(proto_decoder_t *dec, const uint8_t *data, size_t len, uint32_t now_ms,
                        proto_frame_cb_t cb, void *ctx);

/* Drops a partial frame if no byte arrived for PROTO_BYTE_TIMEOUT_MS. Call periodically. */
void proto_decoder_poll(proto_decoder_t *dec, uint32_t now_ms, proto_frame_cb_t cb, void *ctx);

#endif /* PROTO_FRAME_H */
