/**
  ******************************************************************************
  * @file    node_diag.c
  * @brief   Reset cause, sticky error flags and reset-surviving records.
  ******************************************************************************
  */

#include "node_diag.h"

#include <string.h>

#include "stm32wlxx_hal.h"

#define DIAG_MAGIC_BOOTLOADER  0xB007B007UL
#define DIAG_MAGIC_FAULT       0xFA017EC0UL

/* STM32WLE5 system memory (ST bootloader) */
#define SYSMEM_BASE            0x1FFF0000UL

typedef struct
{
  uint32_t bootloader_magic;
  uint32_t fault_magic;
  uint32_t fault_code;
  char fault_task[16];
} diag_noinit_t;

/* SRAM2 section is NOLOAD: not cleared by startup, survives software resets */
static diag_noinit_t s_noinit __attribute__((section(".RAM2_region")));

static reset_cause_t s_reset_cause = RESET_CAUSE_UNKNOWN;
static volatile uint16_t s_flags;

static void jump_to_system_bootloader(void)
{
  /* Runs right after reset: clocks and peripherals are still in reset state,
   * which is what the ST bootloader expects. */
  __disable_irq();
  SysTick->CTRL = 0;
  for (uint32_t i = 0; i < 8U; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }
  __enable_irq();

  void (*sysmem_entry)(void) = (void (*)(void))(*((uint32_t *)(SYSMEM_BASE + 4U)));
  __set_MSP(*(uint32_t *)SYSMEM_BASE);
  sysmem_entry();

  for (;;)
  {
  }
}

void diag_early_boot(void)
{
  /* Order matters: the pin reset flag is set on every reset source */
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0U)
  {
    s_reset_cause = RESET_CAUSE_IWDG;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != 0U)
  {
    s_reset_cause = RESET_CAUSE_WWDG;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != 0U)
  {
    s_reset_cause = RESET_CAUSE_LOW_POWER;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U)
  {
    s_reset_cause = RESET_CAUSE_SOFTWARE;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U)
  {
    s_reset_cause = RESET_CAUSE_POWER_ON;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U)
  {
    s_reset_cause = RESET_CAUSE_PIN;
  }
  __HAL_RCC_CLEAR_RESET_FLAGS();

  if ((s_reset_cause == RESET_CAUSE_POWER_ON) || (s_reset_cause == RESET_CAUSE_UNKNOWN))
  {
    /* SRAM2 content is random after power-up: never trust it */
    memset(&s_noinit, 0, sizeof(s_noinit));
  }
  else if ((s_reset_cause == RESET_CAUSE_SOFTWARE) && (s_noinit.bootloader_magic == DIAG_MAGIC_BOOTLOADER))
  {
    s_noinit.bootloader_magic = 0;
    jump_to_system_bootloader();
  }
}

reset_cause_t diag_reset_cause(void)
{
  return s_reset_cause;
}

uint32_t diag_take_last_fault(char *task_name, uint32_t name_size)
{
  uint32_t code = DIAG_FAULT_NONE;

  if (s_noinit.fault_magic == DIAG_MAGIC_FAULT)
  {
    code = s_noinit.fault_code;
    if ((task_name != NULL) && (name_size > 0U))
    {
      strncpy(task_name, s_noinit.fault_task, name_size - 1U);
      task_name[name_size - 1U] = '\0';
    }
  }
  s_noinit.fault_magic = 0;
  return code;
}

void diag_set_flag(uint16_t flag)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_flags |= flag;
  __set_PRIMASK(primask);
}

uint16_t diag_flags(void)
{
  return s_flags;
}

void diag_fault_reset(uint32_t code, const char *task_name)
{
  __disable_irq();
  s_noinit.fault_code = code;
  memset(s_noinit.fault_task, 0, sizeof(s_noinit.fault_task));
  if (task_name != NULL)
  {
    strncpy(s_noinit.fault_task, task_name, sizeof(s_noinit.fault_task) - 1U);
  }
  s_noinit.fault_magic = DIAG_MAGIC_FAULT;
  NVIC_SystemReset();
}

void diag_enter_bootloader(void)
{
  __disable_irq();
  s_noinit.bootloader_magic = DIAG_MAGIC_BOOTLOADER;
  NVIC_SystemReset();
}
