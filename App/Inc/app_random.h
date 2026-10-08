/**
  ******************************************************************************
  * @file    app_random.h
  * @brief   Random numbers from the TRNG, used for beacon jitter and CAD
  *          backoff. Falls back to a xorshift generator if the TRNG is busy
  *          (shared between tasks) or reports an error.
  ******************************************************************************
  */

#ifndef APP_RANDOM_H
#define APP_RANDOM_H

#include <stdint.h>

uint32_t app_random(void);

/* Uniform value in [min, max] */
uint32_t app_random_range(uint32_t min, uint32_t max);

#endif /* APP_RANDOM_H */
