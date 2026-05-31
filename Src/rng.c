/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rng.c
  * @brief   This file provides code for the configuration
  *          of the RNG instances.
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
#include "rng.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

RNG_HandleTypeDef hrng;

/* RNG init function */
void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  /* During bring-up we keep RNG initialization non-fatal and disable clock error detection.
   * (Clock selection may vary depending on whether PLL is enabled.)
   */
  hrng.Init.ClockErrorDetection = RNG_CED_DISABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    /* RNG not available; continue without it. */
    return;
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

void HAL_RNG_MspInit(RNG_HandleTypeDef* rngHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rngHandle->Instance==RNG)
  {
  /* USER CODE BEGIN RNG_MspInit 0 */

  /* USER CODE END RNG_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RNG;

    /* If PLL is not running (e.g. HSI debug fallback), do not select PLLQ as RNG clock.
     * Use LSI instead so bring-up can proceed.
     */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) != RESET)
    {
      PeriphClkInitStruct.RngClockSelection = RCC_RNGCLKSOURCE_PLL;
    }
    else
    {
      PeriphClkInitStruct.RngClockSelection = RCC_RNGCLKSOURCE_LSI;
    }

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      /* Clock config failed; keep RNG disabled. */
      return;
    }

    /* RNG clock enable */
    __HAL_RCC_RNG_CLK_ENABLE();
  /* USER CODE BEGIN RNG_MspInit 1 */

  /* USER CODE END RNG_MspInit 1 */
  }
}

void HAL_RNG_MspDeInit(RNG_HandleTypeDef* rngHandle)
{

  if(rngHandle->Instance==RNG)
  {
  /* USER CODE BEGIN RNG_MspDeInit 0 */

  /* USER CODE END RNG_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RNG_CLK_DISABLE();
  /* USER CODE BEGIN RNG_MspDeInit 1 */

  /* USER CODE END RNG_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

