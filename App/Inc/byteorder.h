/**
  ******************************************************************************
  * @file    byteorder.h
  * @brief   Explicit big-endian (network order) serialization helpers.
  *          Every multi-byte field of the radio packet and of the serial
  *          frames goes through these, never through struct casts.
  ******************************************************************************
  */

#ifndef BYTEORDER_H
#define BYTEORDER_H

#include <stdint.h>

static inline void be_put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static inline void be_put32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static inline uint16_t be_get16(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t be_get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

#endif /* BYTEORDER_H */
