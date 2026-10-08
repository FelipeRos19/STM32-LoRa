/**
  ******************************************************************************
  * @file    crc16.c
  * @brief   Bitwise CRC-16/CCITT-FALSE. Frames are at most 255 bytes, so the
  *          table-less version is fast enough and trivial to mirror in Kotlin.
  ******************************************************************************
  */

#include "crc16.h"

uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (uint8_t bit = 0; bit < 8U; bit++)
    {
      crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}
