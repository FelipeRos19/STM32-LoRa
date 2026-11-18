/**
  ******************************************************************************
  * @file    alert_packet.h
  * @brief   Radio alert packet (TCC, Quadro 3), big-endian:
  *            port u8 | user_id u32 | seq u16 | lat i32 | lon i32 |
  *            status u8 | battery u8 | msg[0..N]
  *          The message length is not transmitted: it comes from the LoRa
  *          explicit header (packet size - ALERT_HDR_LEN).
  ******************************************************************************
  */

#ifndef ALERT_PACKET_H
#define ALERT_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALERT_PORT_ALERT     0x01U
#define ALERT_PORT_TEST      0x7FU

#define ALERT_HDR_LEN        17U
#define ALERT_MAX_PACKET     255U
#define ALERT_MSG_MAX        (ALERT_MAX_PACKET - ALERT_HDR_LEN)

/* Coordinates in 1e-7 degree */
#define ALERT_LAT_LIMIT      900000000L
#define ALERT_LON_LIMIT      1800000000L

/* User status codes (TCC, Quadro 4). Unknown codes are still forwarded. */
#define ALERT_STATUS_SAFE        0U
#define ALERT_STATUS_DANGER      1U
#define ALERT_STATUS_LOST        2U
#define ALERT_STATUS_INJURED     3U
#define ALERT_STATUS_FIRE        4U
#define ALERT_STATUS_STRANDED    5U
#define ALERT_STATUS_NO_SUPPLIES 6U

#define ALERT_BATTERY_MAX    100U

typedef struct
{
  uint8_t port;
  uint32_t user_id;
  uint16_t seq;
  int32_t lat;
  int32_t lon;
  uint8_t status;
  uint8_t battery;
  uint8_t msg_len;
  uint8_t msg[ALERT_MSG_MAX];
} alert_packet_t;

bool alert_coords_valid(int32_t lat, int32_t lon);

/* Returns the packet size, or 0 if it does not fit in out. */
size_t alert_packet_encode(const alert_packet_t *pkt, uint8_t *out, size_t out_size);

/* Parses a received packet (any port). Returns false if it is too short or too long. */
bool alert_packet_decode(const uint8_t *data, size_t len, alert_packet_t *pkt);

/* Replaces bytes that are not well-formed UTF-8 with '?', in place. */
void alert_msg_sanitize_utf8(uint8_t *msg, size_t len);

#endif /* ALERT_PACKET_H */
