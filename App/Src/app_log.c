/**
  ******************************************************************************
  * @file    app_log.c
  * @brief   FRAME_LOG debug output.
  ******************************************************************************
  */

#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "proto_defs.h"
#include "serial_link.h"

static bool s_scheduler_running;

void app_log_set_scheduler_running(bool running)
{
  s_scheduler_running = running;
}

#if defined(DEBUG)
void app_log(const char *fmt, ...)
{
  char buf[120];
  va_list args;

  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n <= 0)
  {
    return;
  }
  if ((size_t)n >= sizeof(buf))
  {
    n = (int)sizeof(buf) - 1;
  }

  if (s_scheduler_running)
  {
    (void)serial_link_send(FRAME_LOG, (const uint8_t *)buf, (uint8_t)n);
  }
  else
  {
    serial_link_send_early(FRAME_LOG, (const uint8_t *)buf, (uint8_t)n);
  }
}
#endif
