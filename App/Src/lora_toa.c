/**
  ******************************************************************************
  * @file    lora_toa.c
  * @brief   Integer implementation of the LoRa time-on-air formula:
  *            Tsym     = 2^SF / BW
  *            Tpacket  = (npre + 4.25) Tsym + npayload Tsym
  *            npayload = 8 + max(ceil((8PL - 4SF + 28 + 16CRC - 20H) / (4(SF - 2DE))) (CR + 4), 0)
  ******************************************************************************
  */

#include "lora_toa.h"

static bool phy_valid(const lora_phy_t *phy)
{
  return (phy != NULL) && (phy->sf >= 7U) && (phy->sf <= 12U) && (phy->bw <= LORA_BW_500)
         && (phy->cr >= 1U) && (phy->cr <= 4U);
}

bool lora_ldro_enabled(const lora_phy_t *phy)
{
  return ((phy->bw == LORA_BW_125) && (phy->sf >= 11U)) || ((phy->bw == LORA_BW_250) && (phy->sf == 12U));
}

uint32_t lora_toa_us(const lora_phy_t *phy, uint8_t payload_len)
{
  if (!phy_valid(phy))
  {
    return 0;
  }

  /* Tsym in us is exact for these bandwidths: 2^SF * {8, 4, 2} */
  static const uint32_t us_per_chip[] = { 8U, 4U, 2U };
  uint32_t tsym_us = ((uint32_t)1U << phy->sf) * us_per_chip[phy->bw];

  int32_t de = lora_ldro_enabled(phy) ? 1 : 0;
  int32_t num = (8 * (int32_t)payload_len) - (4 * (int32_t)phy->sf) + 28
                + (phy->crc_on ? 16 : 0) - (phy->implicit_header ? 20 : 0);
  int32_t den = 4 * ((int32_t)phy->sf - (2 * de));
  int32_t n_payload = 8;
  if (num > 0)
  {
    n_payload += ((num + den - 1) / den) * ((int32_t)phy->cr + 4);
  }

  /* (npre + 4.25) * Tsym computed as (4 npre + 17) * Tsym / 4; 64-bit so long
   * preambles at SF12 cannot overflow, saturated to UINT32_MAX. */
  uint64_t total_us = ((((4ULL * phy->preamble) + 17ULL) * tsym_us) / 4ULL)
                      + ((uint64_t)n_payload * tsym_us);
  return (total_us > UINT32_MAX) ? UINT32_MAX : (uint32_t)total_us;
}

int16_t lora_max_payload(const lora_phy_t *phy, uint32_t limit_us)
{
  if (!phy_valid(phy))
  {
    return -1;
  }

  int16_t best = -1;
  for (uint16_t pl = 0; pl <= 255U; pl++)
  {
    if (lora_toa_us(phy, (uint8_t)pl) > limit_us)
    {
      break;
    }
    best = (int16_t)pl;
  }
  return best;
}
