/**
  ******************************************************************************
  * @file    supervisor.c
  * @brief   Watchdog supervisor: the only place that refreshes the IWDG.
  ******************************************************************************
  */

#include "supervisor.h"

#include <stdbool.h>

#include "app_log.h"
#include "cmsis_os2.h"
#include "iwdg.h"

#define SUP_PERIOD_MS  1000U

static volatile uint32_t s_last_checkin[SUP_TASK_COUNT];

void supervisor_checkin(sup_task_t task)
{
  if (task < SUP_TASK_COUNT)
  {
    s_last_checkin[task] = osKernelGetTickCount();
  }
}

void supervisor_run(void)
{
  static const char *const names[SUP_TASK_COUNT] = { "serial", "node", "radio" };
  (void)names;   /* only used by APP_LOG in Debug builds */
  uint32_t start = osKernelGetTickCount();
  bool reported = false;

  for (uint32_t i = 0; i < SUP_TASK_COUNT; i++)
  {
    s_last_checkin[i] = start;
  }

  for (;;)
  {
    uint32_t now = osKernelGetTickCount();
    bool all_alive = true;

    for (uint32_t i = 0; i < SUP_TASK_COUNT; i++)
    {
      if ((now - s_last_checkin[i]) > SUP_CHECKIN_TIMEOUT_MS)
      {
        all_alive = false;
        if (!reported)
        {
          APP_LOG("supervisor: task %s stalled, starving IWDG", names[i]);
          reported = true;
        }
      }
    }

    if (all_alive)
    {
      (void)HAL_IWDG_Refresh(&hiwdg);
    }
    osDelay(SUP_PERIOD_MS);
  }
}
