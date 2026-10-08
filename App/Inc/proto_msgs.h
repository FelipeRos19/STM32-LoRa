/**
  ******************************************************************************
  * @file    proto_msgs.h
  * @brief   Payload layouts of the serial frames (see docs/protocolo_serial.md)
  *          and validation of what the app sends. No HAL dependency.
  ******************************************************************************
  */

#ifndef PROTO_MSGS_H
#define PROTO_MSGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alert_packet.h"
#include "lora_toa.h"
#include "proto_defs.h"

#define CONFIG_PAYLOAD_LEN       15U
#define ALERT_STATE_HDR_LEN      10U   /* lat, lon, status, battery */
#define STATUS_PAYLOAD_LEN       14U
#define RX_ALERT_HDR_LEN         19U   /* user_id..snr, msg follows */
#define BOOTLOADER_MAGIC         "BOOT"
#define BOOTLOADER_MAGIC_LEN     4U

/* Longest message accepted from the app or forwarded in RX_ALERT, so an
 * RX_ALERT frame always fits PROTO_MAX_PAYLOAD. */
#define PROTO_MSG_MAX            231U

#define CONFIG_INTERVAL_MIN_S    30U
#define CONFIG_INTERVAL_MAX_S    3600U
#define CONFIG_INTERVAL_DEF_S    300U
#define CONFIG_POWER_MIN_DBM     (-9)
#define CONFIG_POWER_MAX_DBM     22
#define CONFIG_PREAMBLE_MIN      6U
#define CONFIG_PREAMBLE_MAX      1024U

#define BATTERY_UNKNOWN          0xFFU

typedef struct
{
  uint8_t role;          /* node_role_t */
  uint32_t user_id;
  uint16_t interval_s;
  uint8_t sf;
  uint8_t bw;            /* LORA_BW_x */
  uint8_t cr;
  int8_t tx_power;
  uint16_t preamble;
  uint8_t hop;           /* 0: fixed channel (frequency hopping is a later phase) */
} node_config_t;

typedef struct
{
  int32_t lat;
  int32_t lon;
  uint8_t status;
  uint8_t battery;
  uint8_t msg_len;
  uint8_t msg[PROTO_MSG_MAX];
} alert_state_t;

typedef struct
{
  uint8_t ack_type;
  uint8_t result;        /* proto_result_t */
  uint8_t state;         /* node_state_t */
  uint8_t role;          /* node_role_t */
  uint8_t max_msg_len;
  uint16_t last_seq;
  uint16_t err_flags;
  uint8_t reset_cause;   /* reset_cause_t */
} node_status_t;

/* Radio defaults used before the first CONFIG (SF9/125 kHz, 4/5, 14 dBm). */
void node_config_defaults(node_config_t *cfg);

lora_phy_t node_config_phy(const node_config_t *cfg);

/* Full validation, including the 400 ms time-on-air limit. */
proto_result_t node_config_validate(const node_config_t *cfg);

/* Longest message that keeps the alert packet within the dwell limit (0 if invalid). */
uint8_t node_config_max_msg(const node_config_t *cfg);

proto_result_t proto_parse_config(const uint8_t *payload, uint8_t len, node_config_t *cfg);
size_t proto_build_config(const node_config_t *cfg, uint8_t *out, size_t out_size);

/* Parses ALERT_START/UPDATE. The message is checked against max_msg and sanitized to UTF-8. */
proto_result_t proto_parse_alert_state(const uint8_t *payload, uint8_t len, uint8_t max_msg,
                                       alert_state_t *st);

size_t proto_build_status(const node_status_t *st, uint8_t *out, size_t out_size);

/* RX_ALERT from a received packet; messages longer than PROTO_MSG_MAX are truncated. */
size_t proto_build_rx_alert(const alert_packet_t *pkt, int16_t rssi, int8_t snr,
                            uint8_t *out, size_t out_size);

#endif /* PROTO_MSGS_H */
