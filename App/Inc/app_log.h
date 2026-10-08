/**
  ******************************************************************************
  * @file    app_log.h
  * @brief   Debug text sent to the app inside FRAME_LOG frames, so it never
  *          corrupts the binary protocol. Compiled out in Release builds.
  ******************************************************************************
  */

#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>

#if defined(DEBUG)
void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define APP_LOG(...)  app_log(__VA_ARGS__)
#else
#define APP_LOG(...)  do { } while (0)
#endif

/* Before the scheduler starts, logs are sent with a blocking UART write. */
void app_log_set_scheduler_running(bool running);

#endif /* APP_LOG_H */
