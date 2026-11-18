/**
  ******************************************************************************
  * @file    app_random.c
  * @brief   TRNG wrapper with a software fallback.
  ******************************************************************************
  */

#include "app_random.h"

#include "rng.h"

static uint32_t s_xorshift = 0x9E3779B9UL;

uint32_t app_random(void)
{
  uint32_t value;

  if (HAL_RNG_GenerateRandomNumber(&hrng, &value) == HAL_OK)
  {
    s_xorshift ^= value;   /* keep the fallback seeded with real entropy */
    return value;
  }

  s_xorshift ^= s_xorshift << 13;
  s_xorshift ^= s_xorshift >> 17;
  s_xorshift ^= s_xorshift << 5;
  return s_xorshift;
}

uint32_t app_random_range(uint32_t min, uint32_t max)
{
  if (max <= min)
  {
    return min;
  }
  return min + (app_random() % (max - min + 1U));
}
