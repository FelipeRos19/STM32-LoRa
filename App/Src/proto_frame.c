/**
  ******************************************************************************
  * @file    proto_frame.c
  * @brief   Serial frame encoder and resynchronising decoder.
  ******************************************************************************
  */

#include "proto_frame.h"

#include <string.h>

#include "byteorder.h"
#include "crc16.h"

enum
{
  ST_SOF = 0,
  ST_TYPE,
  ST_LEN,
  ST_PAYLOAD,
  ST_CRC_HI,
  ST_CRC_LO,
};

size_t proto_frame_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t out_size)
{
  size_t total = (size_t)len + PROTO_OVERHEAD;

  if ((len > PROTO_MAX_PAYLOAD) || (out_size < total) || ((len > 0U) && (payload == NULL)))
  {
    return 0;
  }

  out[0] = PROTO_SOF;
  out[1] = type;
  out[2] = len;
  if (len > 0U)
  {
    memcpy(&out[3], payload, len);
  }
  be_put16(&out[3U + len], crc16(&out[1], 2U + (size_t)len));
  return total;
}

static void dec_reset(proto_decoder_t *dec)
{
  dec->state = ST_SOF;
  dec->raw_len = 0;
}

void proto_decoder_init(proto_decoder_t *dec)
{
  memset(dec, 0, sizeof(*dec));
  dec_reset(dec);
}

/* Consumes one byte. Returns true when the current candidate is invalid and
 * the bytes after its SOF must be rescanned. */
static bool dec_step(proto_decoder_t *dec, uint8_t b, proto_frame_cb_t cb, void *ctx)
{
  if (dec->state == ST_SOF)
  {
    if (b == PROTO_SOF)
    {
      dec->raw[0] = b;
      dec->raw_len = 1;
      dec->state = ST_TYPE;
    }
    return false;
  }

  dec->raw[dec->raw_len++] = b;

  switch (dec->state)
  {
    case ST_TYPE:
      dec->type = b;
      dec->state = ST_LEN;
      break;

    case ST_LEN:
      if (b > PROTO_MAX_PAYLOAD)
      {
        cb(ctx, PROTO_EVT_ERR_LEN, 0, NULL, 0);
        return true;
      }
      dec->len = b;
      dec->state = (b > 0U) ? ST_PAYLOAD : ST_CRC_HI;
      break;

    case ST_PAYLOAD:
      if (dec->raw_len == (3U + dec->len))
      {
        dec->state = ST_CRC_HI;
      }
      break;

    case ST_CRC_HI:
      dec->state = ST_CRC_LO;
      break;

    case ST_CRC_LO:
    default:
      dec->crc_rx = be_get16(&dec->raw[3U + dec->len]);
      if (dec->crc_rx == crc16(&dec->raw[1], 2U + (size_t)dec->len))
      {
        cb(ctx, PROTO_EVT_FRAME, dec->type, &dec->raw[3], dec->len);
        dec_reset(dec);
        return false;
      }
      cb(ctx, PROTO_EVT_ERR_CRC, 0, NULL, 0);
      return true;
  }
  return false;
}

/* Rescans everything that followed the SOF of the current candidate. Each new
 * failure restarts right after its own SOF, so the scan always moves forward.
 * With drop_partial the bytes are stale (timeout) and a trailing incomplete
 * candidate is discarded instead of waiting for more data. */
static void dec_rescan(proto_decoder_t *dec, bool drop_partial, proto_frame_cb_t cb, void *ctx)
{
  size_t n = (dec->raw_len > 0U) ? ((size_t)dec->raw_len - 1U) : 0U;
  memcpy(dec->work, &dec->raw[1], n);
  dec_reset(dec);

  size_t i = 0;
  size_t start = 0;
  for (;;)
  {
    while (i < n)
    {
      if (dec->state == ST_SOF)
      {
        start = i;
      }
      bool failed = dec_step(dec, dec->work[i], cb, ctx);
      i++;
      if (failed)
      {
        dec_reset(dec);
        i = start + 1U;
      }
    }

    if (!drop_partial || (dec->state == ST_SOF))
    {
      break;
    }
    /* A stale incomplete candidate counts as a failure too: frames may hide inside it */
    dec_reset(dec);
    i = start + 1U;
  }
}

static void dec_process(proto_decoder_t *dec, uint8_t b, proto_frame_cb_t cb, void *ctx)
{
  if (dec_step(dec, b, cb, ctx))
  {
    dec_rescan(dec, false, cb, ctx);
  }
}

static bool dec_timed_out(const proto_decoder_t *dec, uint32_t now_ms)
{
  return (dec->state != ST_SOF) && ((uint32_t)(now_ms - dec->last_byte_ms) > PROTO_BYTE_TIMEOUT_MS);
}

/* A stalled candidate (e.g. a spurious SOF with a large len) may be hiding
 * real frames: report the timeout, recover what is complete, drop the rest. */
static void dec_check_timeout(proto_decoder_t *dec, uint32_t now_ms, proto_frame_cb_t cb, void *ctx)
{
  if (dec_timed_out(dec, now_ms))
  {
    cb(ctx, PROTO_EVT_TIMEOUT, 0, NULL, 0);
    dec_rescan(dec, true, cb, ctx);
  }
}

void proto_decoder_feed(proto_decoder_t *dec, const uint8_t *data, size_t len, uint32_t now_ms,
                        proto_frame_cb_t cb, void *ctx)
{
  if (len == 0U)
  {
    return;
  }
  dec_check_timeout(dec, now_ms, cb, ctx);
  dec->last_byte_ms = now_ms;

  for (size_t i = 0; i < len; i++)
  {
    dec_process(dec, data[i], cb, ctx);
  }
}

void proto_decoder_poll(proto_decoder_t *dec, uint32_t now_ms, proto_frame_cb_t cb, void *ctx)
{
  dec_check_timeout(dec, now_ms, cb, ctx);
}
