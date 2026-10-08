/**
  ******************************************************************************
  * @file    lora_test.h
  * @brief   Events exchanged between the SubGHz radio callbacks (ISR context)
  *          and the UART task during LoRa bring-up.
  ******************************************************************************
  */

#ifndef __LORA_TEST_H__
#define __LORA_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "cmsis_os2.h"

/* Bytes of RX payload copied into each event (longer packets are truncated). */
#define LORA_EVT_MAX_PAYLOAD  64U

typedef enum
{
  LORA_EVT_TX_DONE = 0,
  LORA_EVT_TX_TIMEOUT,
  LORA_EVT_RX_DONE,
  LORA_EVT_RX_TIMEOUT,
  LORA_EVT_RX_ERROR,
} lora_evt_type_t;

typedef struct
{
  lora_evt_type_t type;
  int16_t rssi;
  int8_t snr;
  uint16_t size;
  uint8_t payload[LORA_EVT_MAX_PAYLOAD];
} lora_evt_t;

/* Created in MX_FREERTOS_Init(), filled by the radio callbacks. */
extern osMessageQueueId_t loraEvtQueueHandle;

#ifdef __cplusplus
}
#endif

#endif /* __LORA_TEST_H__ */
