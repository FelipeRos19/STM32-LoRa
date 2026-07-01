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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "crypto_test.h"   /* TEMP: crypto bring-up test, remove later */
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
  osEventFlagsSet(appEventFlagsHandle, APP_EVT_RADIO_READY);

  /* USER CODE BEGIN StartDefaultTask */
  /* --- TEMP: crypto bring-up test (remove with crypto_test.c/.h) --- */
  (void)crypto_test_run();
  /* --- end TEMP --- */

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
  /* Wait until radio is initialized in default task */
  (void)osEventFlagsWait(appEventFlagsHandle, APP_EVT_RADIO_READY, osFlagsWaitAny, osWaitForever);

  /* Periodic TX beacon to validate the link (disable once mesh is in place) */
  for(;;)
  {
    uint8_t msg[32];
    int len = snprintf((char*)msg, sizeof(msg), "BEACON %lu", (unsigned long)g_beacon_counter++);
    if (len > 0)
    {
      (void)Radio.Send(msg, (uint8_t)len);
    }

    osDelay(2000);
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
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"Type B to jump bootloader\r\n", (uint16_t)(sizeof("Type B to jump bootloader\r\n") - 1U), 100);

  uint8_t b;
  for(;;)
  {
    if (HAL_UART_Receive(&huart2, &b, 1, 100) == HAL_OK)
    {
      if (b == 'B')
      {
        (void)HAL_UART_Transmit(&huart2, (uint8_t*)"Jumping to bootloader...\r\n", (uint16_t)(sizeof("Jumping to bootloader...\r\n") - 1U), 100);
        osDelay(50);
        JumpToBootloader();
      }

      /* simple echo */
      (void)HAL_UART_Transmit(&huart2, &b, 1, 100);
    }

    (void)HAL_IWDG_Refresh(&hiwdg);
    osDelay(1);
  }
  /* USER CODE END StartUARTTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
