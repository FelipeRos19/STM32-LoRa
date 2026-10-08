/**
  ******************************************************************************
  * @file    alert_packet.c
  * @brief   Radio alert packet serialization.
  ******************************************************************************
  */

#include "alert_packet.h"

#include <string.h>

#include "byteorder.h"

bool alert_coords_valid(int32_t lat, int32_t lon)
{
  return (lat >= -ALERT_LAT_LIMIT) && (lat <= ALERT_LAT_LIMIT)
         && (lon >= -ALERT_LON_LIMIT) && (lon <= ALERT_LON_LIMIT);
}

size_t alert_packet_encode(const alert_packet_t *pkt, uint8_t *out, size_t out_size)
{
  size_t total = (size_t)ALERT_HDR_LEN + pkt->msg_len;

  if ((pkt->msg_len > ALERT_MSG_MAX) || (out_size < total))
  {
    return 0;
  }

  out[0] = pkt->port;
  be_put32(&out[1], pkt->user_id);
  be_put16(&out[5], pkt->seq);
  be_put32(&out[7], (uint32_t)pkt->lat);
  be_put32(&out[11], (uint32_t)pkt->lon);
  out[15] = pkt->status;
  out[16] = pkt->battery;
  memcpy(&out[ALERT_HDR_LEN], pkt->msg, pkt->msg_len);
  return total;
}

bool alert_packet_decode(const uint8_t *data, size_t len, alert_packet_t *pkt)
{
  if ((len < ALERT_HDR_LEN) || (len > ALERT_MAX_PACKET))
  {
    return false;
  }

  pkt->port = data[0];
  pkt->user_id = be_get32(&data[1]);
  pkt->seq = be_get16(&data[5]);
  pkt->lat = (int32_t)be_get32(&data[7]);
  pkt->lon = (int32_t)be_get32(&data[11]);
  pkt->status = data[15];
  pkt->battery = data[16];
  pkt->msg_len = (uint8_t)(len - ALERT_HDR_LEN);
  memcpy(pkt->msg, &data[ALERT_HDR_LEN], pkt->msg_len);
  return true;
}

void alert_msg_sanitize_utf8(uint8_t *msg, size_t len)
{
  size_t i = 0;

  while (i < len)
  {
    uint8_t c = msg[i];
    size_t extra;
    uint32_t min_cp;

    if (c < 0x80U)
    {
      i++;
      continue;
    }
    else if ((c & 0xE0U) == 0xC0U)
    {
      extra = 1;
      min_cp = 0x80U;
    }
    else if ((c & 0xF0U) == 0xE0U)
    {
      extra = 2;
      min_cp = 0x800U;
    }
    else if ((c & 0xF8U) == 0xF0U)
    {
      extra = 3;
      min_cp = 0x10000U;
    }
    else
    {
      msg[i++] = '?';
      continue;
    }

    bool ok = true;
    uint32_t cp = (uint32_t)c & (0x3FU >> extra);
    for (size_t k = 1; ok && (k <= extra); k++)
    {
      if ((i + k) >= len || ((msg[i + k] & 0xC0U) != 0x80U))
      {
        ok = false;
      }
      else
      {
        cp = (cp << 6) | (msg[i + k] & 0x3FU);
      }
    }
    /* Reject overlong forms, surrogates and code points above U+10FFFF */
    if (ok && ((cp < min_cp) || (cp > 0x10FFFFU) || ((cp >= 0xD800U) && (cp <= 0xDFFFU))))
    {
      ok = false;
    }

    if (ok)
    {
      i += extra + 1U;
    }
    else
    {
      msg[i++] = '?';
    }
  }
}
