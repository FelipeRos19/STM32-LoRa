/**
  ******************************************************************************
  * @file    supervisor.h
  * @brief   Task liveness supervisor. Each application task checks in from
  *          its main loop; the IWDG is refreshed only while all of them are
  *          alive, so a hung task resets the node instead of being hidden by
  *          another task feeding the watchdog.
  ******************************************************************************
  */

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

typedef enum
{
  SUP_TASK_SERIAL = 0,
  SUP_TASK_NODE,
  SUP_TASK_RADIO,
  SUP_TASK_COUNT,
} sup_task_t;

/* Max time between check-ins before the watchdog is starved (IWDG ~32 s on top) */
#define SUP_CHECKIN_TIMEOUT_MS  10000U

void supervisor_checkin(sup_task_t task);

/* Runs forever in the default task. */
void supervisor_run(void);

#endif /* SUPERVISOR_H */
