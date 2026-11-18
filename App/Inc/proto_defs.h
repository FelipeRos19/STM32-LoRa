/**
  ******************************************************************************
  * @file    proto_defs.h
  * @brief   Constants shared by the serial protocol (node <-> Android app) and
  *          the node state machine. docs/protocolo_serial.md is the reference
  *          document; keep both in sync.
  ******************************************************************************
  */

#ifndef PROTO_DEFS_H
#define PROTO_DEFS_H

#define PROTO_VERSION              1U

#define FW_VERSION_MAJOR           1U
#define FW_VERSION_MINOR           0U
#define FW_VERSION_PATCH           0U

/* Frame types: app -> node */
#define FRAME_CONFIG               0x01U
#define FRAME_ALERT_START          0x02U
#define FRAME_UPDATE               0x03U
#define FRAME_ALERT_STOP           0x04U
#define FRAME_GET_STATUS           0x05U
#define FRAME_ENTER_BOOTLOADER     0x0FU

/* Frame types: node -> app */
#define FRAME_RX_ALERT             0x81U
#define FRAME_STATUS               0x82U
#define FRAME_LOG                  0x83U

/* STATUS.ack_type for unsolicited (event) reports */
#define ACK_EVENT                  0x00U

/* STATUS.result */
typedef enum
{
  RESULT_OK      = 0,
  RESULT_E_CRC   = 1,
  RESULT_E_LEN   = 2,
  RESULT_E_PARAM = 3,
  RESULT_E_STATE = 4,
  RESULT_E_TOA   = 5,
  RESULT_E_FLASH = 6,
  RESULT_E_TYPE  = 7,
  RESULT_E_BUSY  = 8,
  RESULT_E_RADIO = 9,
} proto_result_t;

/* STATUS.state */
typedef enum
{
  NODE_STATE_WAIT_CONFIG  = 0,
  NODE_STATE_BEACON_IDLE  = 1,
  NODE_STATE_BEACON_ALERT = 2,
  NODE_STATE_SNIFFER_RX   = 3,
} node_state_t;

/* CONFIG.role / STATUS.role */
typedef enum
{
  NODE_ROLE_NONE    = 0,
  NODE_ROLE_BEACON  = 1,
  NODE_ROLE_SNIFFER = 2,
} node_role_t;

/* STATUS.err_flags (sticky until the next reset) */
#define ERR_FLAG_SERIAL_CRC        (1U << 0)  /* at least one corrupted frame */
#define ERR_FLAG_SERIAL_OVERFLOW   (1U << 1)  /* UART RX ring buffer overflow */
#define ERR_FLAG_NVM_READ          (1U << 2)  /* stored config invalid/corrupt */
#define ERR_FLAG_NVM_WRITE         (1U << 3)  /* flash write failed */
#define ERR_FLAG_RADIO_TX_TIMEOUT  (1U << 4)
#define ERR_FLAG_RADIO_RESET       (1U << 5)  /* radio re-initialised after errors */
#define ERR_FLAG_CAD_BUSY          (1U << 6)  /* sent after exhausting CAD retries */
#define ERR_FLAG_SERIAL_TX_DROP    (1U << 7)  /* TX queue full, frame dropped */
#define ERR_FLAG_LSE_FALLBACK      (1U << 8)  /* 32.768 kHz crystal failed, RTC on LSI */

/* STATUS.reset_cause */
typedef enum
{
  RESET_CAUSE_UNKNOWN  = 0,
  RESET_CAUSE_POWER_ON = 1,
  RESET_CAUSE_PIN      = 2,
  RESET_CAUSE_SOFTWARE = 3,
  RESET_CAUSE_IWDG     = 4,
  RESET_CAUSE_WWDG     = 5,
  RESET_CAUSE_LOW_POWER = 6,
  RESET_CAUSE_BROWNOUT = 7,
} reset_cause_t;

#endif /* PROTO_DEFS_H */
