/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "aes.h"
#include "crc.h"
#include "dma.h"
#include "iwdg.h"
#include "rng.h"
#include "rtc.h"
#include "app_subghz_phy.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

/* USER CODE BEGIN PV */
/* Boot progress marker to debug early hangs without UART/SWD.
 * Updated throughout `main()`; `Error_Handler()` blinks it out on PB3.
 */
volatile uint32_t g_boot_stage = 0;
volatile uint8_t g_iwdg_started = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void JumpToBootloader(void)
{
  uint32_t i=0;

  /* Disable all interrupts */
  __disable_irq();

  /* Disable SysTick */
  SysTick->CTRL = 0;

  /* Clear all interrupt pending bits */
  for(i=0; i<5; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  __enable_irq();

  /* STM32WLE5 system memory address */
  void (*SysMemBootJump)(void) = (void (*)(void)) (*((uint32_t *) (0x1FFF0004)));

  /* Set MSP */
  __set_MSP(*(uint32_t *)0x1FFF0000);

  /* Jump */
  SysMemBootJump();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  g_boot_stage = 1;

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  g_boot_stage = 2;
  SystemClock_Config();
  g_boot_stage = 3;

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  g_boot_stage = 10;
  MX_GPIO_Init();
  g_boot_stage = 11;
  MX_DMA_Init();
  g_boot_stage = 12;
  MX_USART2_UART_Init();
  g_boot_stage = 13;

  /* Early UART banner to confirm we reached peripheral init (before RTOS). */
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: after USART2 init\r\n", (uint16_t)(sizeof("BOOT: after USART2 init\r\n") - 1U), 200);
  g_boot_stage = 14;

  g_boot_stage = 20;
  MX_AES_Init();
  g_boot_stage = 21;
  MX_RTC_Init();
  g_boot_stage = 22;

  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: after RTC init\r\n", (uint16_t)(sizeof("BOOT: after RTC init\r\n") - 1U), 200);
  g_boot_stage = 23;
  g_boot_stage = 30;
  MX_CRC_Init();
  g_boot_stage = 31;
  MX_IWDG_Init();
  g_boot_stage = 32;

  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: IWDG started\r\n", (uint16_t)(sizeof("BOOT: IWDG started\r\n") - 1U), 200);
  g_boot_stage = 33;

  g_iwdg_started = 1U;

  /* Refresh IWDG during long init phases before RTOS tasks start refreshing it. */
  (void)HAL_IWDG_Refresh(&hiwdg);

  /* RNG init is optional for LoRa bring-up. It can fail depending on clock-tree
   * configuration (PLL off in debug mode). Skip it for now.
   */
  g_boot_stage = 40;
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: RNG skipped\r\n", (uint16_t)(sizeof("BOOT: RNG skipped\r\n") - 1U), 200);
  g_boot_stage = 41;

  MX_TIM2_Init();
  g_boot_stage = 42;
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: after TIM2 init\r\n", (uint16_t)(sizeof("BOOT: after TIM2 init\r\n") - 1U), 200);
  (void)HAL_IWDG_Refresh(&hiwdg);
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  g_boot_stage = 50;
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  g_boot_stage = 51;
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: kernel init ok\r\n", (uint16_t)(sizeof("BOOT: kernel init ok\r\n") - 1U), 200);
  (void)HAL_IWDG_Refresh(&hiwdg);

  MX_FREERTOS_Init();
  g_boot_stage = 52;
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: RTOS objects ok\r\n", (uint16_t)(sizeof("BOOT: RTOS objects ok\r\n") - 1U), 200);
  (void)HAL_IWDG_Refresh(&hiwdg);

  /* Start scheduler */
  (void)HAL_UART_Transmit(&huart2, (uint8_t*)"BOOT: starting RTOS\r\n", (uint16_t)(sizeof("BOOT: starting RTOS\r\n") - 1U), 200);
  g_boot_stage = 60;
  osKernelStart();
  g_boot_stage = 61;

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint8_t buf;

  while (1)
  {
    if (HAL_UART_Receive(&huart2, &buf, 1, 100) == HAL_OK)
    {
      if (buf == 'B')
      {
        HAL_UART_Transmit(&huart2, (uint8_t*)"Jumping to bootloader...\r\n", 26, 100);
        HAL_Delay(100);
        JumpToBootloader();
      }
      HAL_UART_Transmit(&huart2, &buf, 1, 100);
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  /* This project was generated with HSE as PLL source (expects 32 MHz HSE).
   * Many E77 module carriers do not populate an HSE crystal/TCXO on the MCU pins.
   * To make bring-up robust:
   *  1) Try HSE+PLL (48 MHz)
   *  2) If HSE fails, fall back to plain HSI (16 MHz) without PLL.
   *
   * The HSI fallback is intentionally simple because it should always produce
   * a stable UART baud rate for debugging.
   */
  uint8_t use_pll = 1U;

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
  RCC_OscInitStruct.HSEDiv = RCC_HSE_DIV1;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
  RCC_OscInitStruct.PLL.PLLN = 6;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    /* Fallback: HSI16 for CPU clock, no PLL. */
    use_pll = 0U;

    RCC_OscInitStruct = (RCC_OscInitTypeDef){0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
      Error_Handler();
    }
    /* HSE is left disabled here.  On E77-900M22S the TCXO is powered by the
     * radio's VDDTCXO output (IS_TCXO_SUPPORTED=1); it becomes usable only
     * after SUBGRF_SetTcxoMode() runs inside SubghzApp_Init.  The CPU runs
     * on HSI16; the radio manages HSE for its own RF use. */
  }

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK3|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = (use_pll != 0U) ? RCC_SYSCLKSOURCE_PLLCLK : RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK3Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, (use_pll != 0U) ? FLASH_LATENCY_2 : FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM16 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM16)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();

  /* Try to indicate a fatal error even if UART/RTOS is dead.
   * PB3 is used as a debug LED in this project.
   */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIOB->MODER = (GPIOB->MODER & ~(3U << (3U * 2U))) | (1U << (3U * 2U)); /* PB3 output */

  while (1)
  {
    /* Blink out the last boot stage in decimal pulses.
     * Pattern: N short blinks then a pause.
     */
    uint32_t n = g_boot_stage;
    if (n == 0U) { n = 99U; }

    for (uint32_t k = 0; k < n; k++)
    {
      GPIOB->BSRR = (1U << 3);
      for (volatile uint32_t i = 0; i < 40000U; i++) { __NOP(); }
      GPIOB->BSRR = (1U << (3 + 16));
      for (volatile uint32_t i = 0; i < 70000U; i++) { __NOP(); }
      if (g_iwdg_started != 0U) { (void)HAL_IWDG_Refresh(&hiwdg); }
    }

    for (volatile uint32_t i = 0; i < 600000U; i++) { __NOP(); }
    if (g_iwdg_started != 0U) { (void)HAL_IWDG_Refresh(&hiwdg); }
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
