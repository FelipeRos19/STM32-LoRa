/**
  ******************************************************************************
  * @file    proto_msgs.c
  * @brief   Serial payload encoding/decoding and configuration validation.
  ******************************************************************************
  */

#include "proto_msgs.h"

#include <string.h>

#include "byteorder.h"

void node_config_defaults(node_config_t *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->role = NODE_ROLE_NONE;
  cfg->interval_s = CONFIG_INTERVAL_DEF_S;
  cfg->sf = 9U;
  cfg->bw = LORA_BW_125K;
  cfg->cr = 1U;
  cfg->tx_power = 14;
  cfg->preamble = 8U;
  cfg->hop = 0U;
}

lora_phy_t node_config_phy(const node_config_t *cfg)
{
  lora_phy_t phy = {
    .sf = cfg->sf,
    .bw = cfg->bw,
    .cr = cfg->cr,
    .preamble = cfg->preamble,
    .crc_on = true,
    .implicit_header = false,
  };
  return phy;
}

uint8_t node_config_max_msg(const node_config_t *cfg)
{
  lora_phy_t phy = node_config_phy(cfg);
  int16_t max_pl = lora_max_payload(&phy, LORA_DWELL_LIMIT_US);

  if (max_pl < (int16_t)ALERT_HDR_LEN)
  {
    return 0;
  }
  int16_t max_msg = (int16_t)(max_pl - (int16_t)ALERT_HDR_LEN);
  return (max_msg > (int16_t)PROTO_MSG_MAX) ? (uint8_t)PROTO_MSG_MAX : (uint8_t)max_msg;
}

proto_result_t node_config_validate(const node_config_t *cfg)
{
  if ((cfg->role != NODE_ROLE_BEACON) && (cfg->role != NODE_ROLE_SNIFFER))
  {
    return RESULT_E_PARAM;
  }
  if ((cfg->role == NODE_ROLE_BEACON)
      && ((cfg->user_id == 0U) || (cfg->interval_s < CONFIG_INTERVAL_MIN_S)
          || (cfg->interval_s > CONFIG_INTERVAL_MAX_S)))
  {
    return RESULT_E_PARAM;
  }
  if ((cfg->sf < 7U) || (cfg->sf > 12U) || (cfg->bw > LORA_BW_500K) || (cfg->cr < 1U) || (cfg->cr > 4U)
      || (cfg->tx_power < CONFIG_POWER_MIN_DBM) || (cfg->tx_power > CONFIG_POWER_MAX_DBM)
      || (cfg->preamble < CONFIG_PREAMBLE_MIN) || (cfg->preamble > CONFIG_PREAMBLE_MAX)
      || (cfg->hop != 0U))
  {
    return RESULT_E_PARAM;
  }

  /* The fixed 17-byte header alone must fit the dwell limit */
  lora_phy_t phy = node_config_phy(cfg);
  if (lora_max_payload(&phy, LORA_DWELL_LIMIT_US) < (int16_t)ALERT_HDR_LEN)
  {
    return RESULT_E_TOA;
  }
  return RESULT_OK;
}

proto_result_t proto_parse_config(const uint8_t *payload, uint8_t len, node_config_t *cfg)
{
  /* Longer payloads are accepted so a newer app can append fields */
  if (len < CONFIG_PAYLOAD_LEN)
  {
    return RESULT_E_LEN;
  }
  if (payload[0] != PROTO_VERSION)
  {
    return RESULT_E_PARAM;
  }

  node_config_t tmp;
  tmp.role = payload[1];
  tmp.user_id = be_get32(&payload[2]);
  tmp.interval_s = be_get16(&payload[6]);
  tmp.sf = payload[8];
  tmp.bw = payload[9];
  tmp.cr = payload[10];
  tmp.tx_power = (int8_t)payload[11];
  tmp.preamble = be_get16(&payload[12]);
  tmp.hop = payload[14];

  proto_result_t res = node_config_validate(&tmp);
  if (res == RESULT_OK)
  {
    *cfg = tmp;
  }
  return res;
}

size_t proto_build_config(const node_config_t *cfg, uint8_t *out, size_t out_size)
{
  if (out_size < CONFIG_PAYLOAD_LEN)
  {
    return 0;
  }
  out[0] = PROTO_VERSION;
  out[1] = cfg->role;
  be_put32(&out[2], cfg->user_id);
  be_put16(&out[6], cfg->interval_s);
  out[8] = cfg->sf;
  out[9] = cfg->bw;
  out[10] = cfg->cr;
  out[11] = (uint8_t)cfg->tx_power;
  be_put16(&out[12], cfg->preamble);
  out[14] = cfg->hop;
  return CONFIG_PAYLOAD_LEN;
}

proto_result_t proto_parse_alert_state(const uint8_t *payload, uint8_t len, uint8_t max_msg,
                                       alert_state_t *st)
{
  if (len < ALERT_STATE_HDR_LEN)
  {
    return RESULT_E_LEN;
  }

  int32_t lat = (int32_t)be_get32(&payload[0]);
  int32_t lon = (int32_t)be_get32(&payload[4]);
  uint8_t battery = payload[9];
  if (!alert_coords_valid(lat, lon) || ((battery > ALERT_BATTERY_MAX) && (battery != BATTERY_UNKNOWN)))
  {
    return RESULT_E_PARAM;
  }

  size_t msg_len = (size_t)len - ALERT_STATE_HDR_LEN;
  if ((msg_len > max_msg) || (msg_len > PROTO_MSG_MAX))
  {
    return RESULT_E_TOA;
  }

  st->lat = lat;
  st->lon = lon;
  st->status = payload[8];
  st->battery = battery;
  st->msg_len = (uint8_t)msg_len;
  memcpy(st->msg, &payload[ALERT_STATE_HDR_LEN], msg_len);
  alert_msg_sanitize_utf8(st->msg, msg_len);
  return RESULT_OK;
}

size_t proto_build_status(const node_status_t *st, uint8_t *out, size_t out_size)
{
  if (out_size < STATUS_PAYLOAD_LEN)
  {
    return 0;
  }
  out[0] = st->ack_type;
  out[1] = st->result;
  out[2] = st->state;
  out[3] = st->role;
  out[4] = FW_VERSION_MAJOR;
  out[5] = FW_VERSION_MINOR;
  out[6] = FW_VERSION_PATCH;
  out[7] = PROTO_VERSION;
  out[8] = st->max_msg_len;
  be_put16(&out[9], st->last_seq);
  be_put16(&out[11], st->err_flags);
  out[13] = st->reset_cause;
  return STATUS_PAYLOAD_LEN;
}

size_t proto_build_rx_alert(const alert_packet_t *pkt, int16_t rssi, int8_t snr,
                            uint8_t *out, size_t out_size)
{
  size_t msg_len = (pkt->msg_len > PROTO_MSG_MAX) ? PROTO_MSG_MAX : pkt->msg_len;
  size_t total = RX_ALERT_HDR_LEN + msg_len;

  if (out_size < total)
  {
    return 0;
  }
  be_put32(&out[0], pkt->user_id);
  be_put16(&out[4], pkt->seq);
  be_put32(&out[6], (uint32_t)pkt->lat);
  be_put32(&out[10], (uint32_t)pkt->lon);
  out[14] = pkt->status;
  out[15] = pkt->battery;
  be_put16(&out[16], (uint16_t)rssi);
  out[18] = (uint8_t)snr;
  memcpy(&out[RX_ALERT_HDR_LEN], pkt->msg, msg_len);
  return total;
}
