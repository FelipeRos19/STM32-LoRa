/**
  ******************************************************************************
  * @file    lora_toa.h
  * @brief   LoRa time on air (TCC eq. 1 and 2, Semtech AN1200.13) and the
  *          largest payload that fits a dwell-time limit.
  ******************************************************************************
  */

#ifndef LORA_TOA_H
#define LORA_TOA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Anatel dwell time limit for 125 kHz LoRa with frequency hopping */
#define LORA_DWELL_LIMIT_US  400000UL

/* Bandwidth codes, same values as the SubGHz_Phy driver (0=125, 1=250, 2=500 kHz) */
#define LORA_BW_125K          0U
#define LORA_BW_250K          1U
#define LORA_BW_500K          2U

typedef struct
{
  uint8_t sf;           /* 7..12 */
  uint8_t bw;           /* LORA_BW_x */
  uint8_t cr;           /* 1 (4/5) .. 4 (4/8) */
  uint16_t preamble;    /* preamble symbols */
  bool crc_on;
  bool implicit_header;
} lora_phy_t;

/* Same rule as the radio driver: LDRO for SF11/SF12 @125 kHz and SF12 @250 kHz. */
bool lora_ldro_enabled(const lora_phy_t *phy);

/* Time on air in microseconds, or 0 for an invalid configuration. */
uint32_t lora_toa_us(const lora_phy_t *phy, uint8_t payload_len);

/* Largest payload (0..255) whose time on air is <= limit_us, or -1 if none fits. */
int16_t lora_max_payload(const lora_phy_t *phy, uint32_t limit_us);

#endif /* LORA_TOA_H */
