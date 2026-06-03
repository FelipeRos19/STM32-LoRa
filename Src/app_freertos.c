/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"

#include "app_subghz_phy.h"
#include "radio.h"
#include "usart.h"
#include "iwdg.h"
#include "lora_test.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 4
};
/* Definitions for radioTask */
osThreadId_t radioTaskHandle;
const osThreadAttr_t radioTask_attributes = {
  .name = "radioTask",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 512 * 4
};
/* Definitions for routingTask */
osThreadId_t routingTaskHandle;
const osThreadAttr_t routingTask_attributes = {
  .name = "routingTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for UARTTask */
osThreadId_t UARTTaskHandle;
const osThreadAttr_t UARTTask_attributes = {
  .name = "UARTTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for radioEventQueue */
osMessageQueueId_t radioEventQueueHandle;

/* LoRa test event queue (RX/TX notifications from radio callbacks) */
osMessageQueueId_t loraEvtQueueHandle;
const osMessageQueueAttr_t loraEvtQueue_attributes = { .name = "loraEvtQueue" };

static volatile uint8_t g_beacon_enabled = 1U;
static volatile uint8_t g_send_ping = 0U;
static volatile uint8_t g_rx_only_mode = 0U;
const osMessageQueueAttr_t radioEventQueue_attributes = {
  .name = "radioEventQueue"
};
/* Definitions for txPacketQueue */
osMessageQueueId_t txPacketQueueHandle;
const osMessageQueueAttr_t txPacketQueue_attributes = {
  .name = "txPacketQueue"
};
/* Definitions for uartRxQueue */
osMessageQueueId_t uartRxQueueHandle;

/* Definitions for appEventFlags */
osEventFlagsId_t appEventFlagsHandle;

#define APP_EVT_RADIO_READY   (1U << 0)

/* Simple TX test payload counter */
static uint32_t g_beacon_counter = 0;
const osMessageQueueAttr_t uartRxQueue_attributes = {
  .name = "uartRxQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartRadioTask(void *argument);
void StartRoutingTask(void *argument);
void StartUARTTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */

  /* Create event flags */
  appEventFlagsHandle = osEventFlagsNew(NULL);
  /* creation of radioEventQueue */
  radioEventQueueHandle = osMessageQueueNew (8, sizeof(uint32_t), &radioEventQueue_attributes);

  /* LoRa RX/TX event queue */
  loraEvtQueueHandle = osMessageQueueNew(8, sizeof(lora_evt_t), &loraEvtQueue_attributes);

  /* creation of txPacketQueue */
  txPacketQueueHandle = osMessageQueueNew (4, sizeof(uint32_t), &txPacketQueue_attributes);

  /* creation of uartRxQueue */
  uartRxQueueHandle = osMessageQueueNew (16, sizeof(uint32_t), &uartRxQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of radioTask */
  radioTaskHandle = osThreadNew(StartRadioTask, NULL, &radioTask_attributes);

  /* creation of routingTask */
  routingTaskHandle = osThreadNew(StartRoutingTask, NULL, &routingTask_attributes);

  /* creation of UARTTask */
  UARTTaskHandle = osThreadNew(StartUARTTask, NULL, &UARTTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for SubGHz_Phy */
  MX_SubGHz_Phy_Init();
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"Radio init OK - in RX\r\n",
                           (uint16_t)(sizeof("Radio init OK - in RX\r\n") - 1U), 200);
  osEventFlagsSet(appEventFlagsHandle, APP_EVT_RADIO_READY);

  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    /* Keep the watchdog happy during bring-up */
    (void)HAL_IWDG_Refresh(&hiwdg);
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartRadioTask */
/**
* @brief Function implementing the radioTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRadioTask */
void StartRadioTask(void *argument)
{
  /* USER CODE BEGIN StartRadioTask */
  (void)osEventFlagsWait(appEventFlagsHandle, APP_EVT_RADIO_READY, osFlagsWaitAny, osWaitForever);

  /* Start in continuous RX. */
  Radio.Rx(0);

  for(;;)
  {
    if (g_rx_only_mode != 0U)
    {
      /* Re-arm RX each cycle so we stay in continuous receive. */
      Radio.Rx(0);
      (void)HAL_IWDG_Refresh(&hiwdg);
      osDelay(2000);
    }
    else
    {
      uint8_t do_tx = 0U;
      uint8_t tx_buf[32];
      uint8_t tx_len = 0U;

      if (g_send_ping != 0U)
      {
        g_send_ping = 0U;
        const uint8_t ping[] = "PING";
        tx_len = (uint8_t)(sizeof(ping) - 1U);
        memcpy(tx_buf, ping, tx_len);
        do_tx = 1U;
      }
      else if (g_beacon_enabled != 0U)
      {
        int len = snprintf((char*)tx_buf, sizeof(tx_buf), "BEACON %lu",
                           (unsigned long)g_beacon_counter++);
        if (len > 0)
        {
          tx_len = (uint8_t)len;
          do_tx = 1U;
        }
      }

      if (do_tx != 0U)
      {
        /*
         * Put the radio in standby first to abort any ongoing RX.  Then yield
         * for one scheduler tick so any ISR that was triggered by a pending
         * radio IRQ (e.g. CRC error from noise) gets to run and clears the
         * radio IRQ status register.  Without this drain step, the ISR could
         * fire inside Radio.Send() and overwrite the DIO IRQ mask back to RX
         * mode, causing TX_DONE to never trigger SUBGHZ_Radio_IRQn.
         */
        Radio.Standby();
        osDelay(1);

        (void)Radio.Send(tx_buf, tx_len);

        /* Wait long enough for the packet to finish (SF7/BW125 << 200 ms). */
        osDelay(200);

        /* Re-arm RX for the remainder of the 2-second window. */
        Radio.Rx(0);

        (void)HAL_IWDG_Refresh(&hiwdg);
        osDelay(1800);
      }
      else
      {
        /* Nothing to transmit — stay in RX. */
        Radio.Rx(0);
        (void)HAL_IWDG_Refresh(&hiwdg);
        osDelay(2000);
      }
    }
  }
  /* USER CODE END StartRadioTask */
}

/* USER CODE BEGIN Header_StartRoutingTask */
/**
* @brief Function implementing the routingTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRoutingTask */
void StartRoutingTask(void *argument)
{
  /* USER CODE BEGIN StartRoutingTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartRoutingTask */
}

/* USER CODE BEGIN Header_StartUARTTask */
/**
* @brief Function implementing the UARTTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUARTTask */
void StartUARTTask(void *argument)
{
  /* USER CODE BEGIN StartUARTTask */
  extern void JumpToBootloader(void);

  /* Print immediately (don’t depend on radio init). */
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"UART task start\r\n", (uint16_t)(sizeof("UART task start\r\n") - 1U), 100);
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"Commands: B=boot  t=toggle beacon  p=ping  r=toggle RX-only\r\n",
                          (uint16_t)(sizeof("Commands: B=boot  t=toggle beacon  p=ping  r=toggle RX-only\r\n") - 1U), 100);

  uint8_t b;
  lora_evt_t evt;

  for(;;)
  {
    /* Drain LoRa event queue and print */
    while (osMessageQueueGet(loraEvtQueueHandle, &evt, NULL, 0) == osOK)
    {
      switch (evt.type)
      {
        case LORA_EVT_TX_DONE:
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"LORA: TX_DONE\r\n", (uint16_t)(sizeof("LORA: TX_DONE\r\n") - 1U), 100);
          break;
        case LORA_EVT_TX_TIMEOUT:
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"LORA: TX_TIMEOUT\r\n", (uint16_t)(sizeof("LORA: TX_TIMEOUT\r\n") - 1U), 100);
          break;
        case LORA_EVT_RX_TIMEOUT:
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"LORA: RX_TIMEOUT\r\n", (uint16_t)(sizeof("LORA: RX_TIMEOUT\r\n") - 1U), 100);
          break;
        case LORA_EVT_RX_ERROR:
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"LORA: RX_ERROR\r\n", (uint16_t)(sizeof("LORA: RX_ERROR\r\n") - 1U), 100);
          break;
        case LORA_EVT_RX_DONE:
        {
          char header[64];
          int n = snprintf(header, sizeof(header), "LORA: RX %uB RSSI=%d SNR=%d\r\n",
                           (unsigned)evt.size, (int)evt.rssi, (int)evt.snr);
          if (n > 0)
          {
            (void)HAL_UART_Transmit(&huart2, (uint8_t*)header, (uint16_t)n, 200);
          }

          /* Print payload as hex */
          for (uint16_t i = 0; i < evt.size; i++)
          {
            char hex[4];
            int hn = snprintf(hex, sizeof(hex), "%02X ", evt.payload[i]);
            if (hn > 0)
            {
              (void)HAL_UART_Transmit(&huart2, (uint8_t*)hex, (uint16_t)hn, 200);
            }
          }
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 200);
          break;
        }
        default:
          break;
      }
    }

    /* Handle UART commands */
    if (HAL_UART_Receive(&huart2, &b, 1, 10) == HAL_OK)
    {
      if (b == 'B')
      {
        (void)HAL_UART_Transmit(&huart2, (uint8_t*)"Jumping to bootloader...\r\n",
                                (uint16_t)(sizeof("Jumping to bootloader...\r\n") - 1U), 100);
        osDelay(50);
        JumpToBootloader();
      }
      else if (b == 't')
      {
        g_beacon_enabled = (g_beacon_enabled == 0U) ? 1U : 0U;
        (void)HAL_UART_Transmit(&huart2, (uint8_t*)(g_beacon_enabled ? "Beacon ON\r\n" : "Beacon OFF\r\n"),
                                (uint16_t)(g_beacon_enabled ? (sizeof("Beacon ON\r\n") - 1U) : (sizeof("Beacon OFF\r\n") - 1U)), 100);
      }
      else if (b == 'p')
      {
        g_send_ping = 1U;
        (void)HAL_UART_Transmit(&huart2, (uint8_t*)"PING queued\r\n", (uint16_t)(sizeof("PING queued\r\n") - 1U), 100);
      }
      else if (b == 'r')
      {
        g_rx_only_mode ^= 1U;
        if (g_rx_only_mode != 0U)
        {
          g_beacon_enabled = 0U;
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"RX-only ON (beacon off)\r\n",
                                  (uint16_t)(sizeof("RX-only ON (beacon off)\r\n") - 1U), 100);
        }
        else
        {
          (void)HAL_UART_Transmit(&huart2, (uint8_t*)"RX-only OFF\r\n",
                                  (uint16_t)(sizeof("RX-only OFF\r\n") - 1U), 100);
        }
      }
      else
      {
        (void)HAL_UART_Transmit(&huart2, &b, 1, 100);
      }
    }

    (void)HAL_IWDG_Refresh(&hiwdg);
    osDelay(5);
  }
  /* USER CODE END StartUARTTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
