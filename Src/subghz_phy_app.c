/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "platform.h"
#include "sys_app.h"
#include "subghz_phy_app.h"
#include "radio.h"
#include "cmsis_os.h"
#include "stm32wlxx_hal.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Simple 915 MHz bring-up defaults (mesh-friendly starting point)
 * - Single channel
 * - BW125/SF7/CR4/5
 * - Explicit header, CRC on
 */
#define LORA_FREQ_HZ                 915100000UL
#define LORA_BW                      0U      /* 0=125kHz, 1=250kHz, 2=500kHz */
#define LORA_SF                      7U      /* 6..12 */
#define LORA_CR                      1U      /* 1=4/5, 2=4/6, 3=4/7, 4=4/8 */
#define LORA_PREAMBLE_LEN            8U
#define LORA_TX_POWER_DBM            14
#define LORA_TX_TIMEOUT_MS           3000UL

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Radio events function pointer */
static RadioEvents_t RadioEvents;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/*!
 * @brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone(void);

/**
  * @brief Function to be executed on Radio Rx Done event
  * @param  payload ptr of buffer received
  * @param  size buffer size
  * @param  rssi
  * @param  LoraSnr_FskCfo
  */
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);

/**
  * @brief Function executed on Radio Tx Timeout event
  */
static void OnTxTimeout(void);

/**
  * @brief Function executed on Radio Rx Timeout event
  */
static void OnRxTimeout(void);

/**
  * @brief Function executed on Radio Rx Error event
  */
static void OnRxError(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
void SubghzApp_Init(void)
{
  /* USER CODE BEGIN SubghzApp_Init_1 */

  /* USER CODE END SubghzApp_Init_1 */

  /* Radio initialization */
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  Radio.Init(&RadioEvents);

  /* USER CODE BEGIN SubghzApp_Init_2 */
  /* LoRa PHY configuration (single-channel, continuous RX) */
  Radio.SetModem(MODEM_LORA);
  Radio.SetPublicNetwork(false);
  Radio.SetChannel(LORA_FREQ_HZ);

  Radio.SetTxConfig(MODEM_LORA,
                    LORA_TX_POWER_DBM,
                    0,
                    LORA_BW,
                    LORA_SF,
                    LORA_CR,
                    LORA_PREAMBLE_LEN,
                    false,
                    true,
                    false,
                    0,
                    false,
                    LORA_TX_TIMEOUT_MS);

  Radio.SetRxConfig(MODEM_LORA,
                    LORA_BW,
                    LORA_SF,
                    LORA_CR,
                    0,
                    LORA_PREAMBLE_LEN,
                    0,
                    false,
                    0,
                    true,
                    false,
                    0,
                    false,
                    true);

  Radio.SetMaxPayloadLength(MODEM_LORA, 255);
  Radio.Rx(0);
  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
  HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_3);
  /* Return to RX after TX */
  Radio.Rx(0);
  /* USER CODE END OnTxDone */
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo)
{
  /* USER CODE BEGIN OnRxDone */
  (void)rssi;
  (void)LoraSnr_FskCfo;

  /* Visual RX indicator */
  HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_3);

  /* For now: just stay in RX. Your mesh layer should copy `payload` and enqueue it. */
  (void)payload;
  (void)size;
  Radio.Rx(0);
  /* USER CODE END OnRxDone */
}

static void OnTxTimeout(void)
{
  /* USER CODE BEGIN OnTxTimeout */
  Radio.Rx(0);
  /* USER CODE END OnTxTimeout */
}

static void OnRxTimeout(void)
{
  /* USER CODE BEGIN OnRxTimeout */
  Radio.Rx(0);
  /* USER CODE END OnRxTimeout */
}

static void OnRxError(void)
{
  /* USER CODE BEGIN OnRxError */
  Radio.Rx(0);
  /* USER CODE END OnRxError */
}

/* USER CODE BEGIN PrFD */

/* USER CODE END PrFD */
