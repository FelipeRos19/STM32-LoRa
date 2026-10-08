/**
  ******************************************************************************
  * @file    nvm_store.c
  * @brief   Append-only flash records with CRC-32 (hardware CRC unit).
  ******************************************************************************
  */

#include "nvm_store.h"

#include <string.h>

#include "byteorder.h"
#include "crc.h"
#include "node_diag.h"
#include "stm32wlxx_hal.h"

#define NVM_MAGIC         0x4C4E5631UL   /* "LNV1" */
#define NVM_VERSION       1U
#define NVM_HDR_LEN       8U             /* magic u32, version u16, payload_len u16 */
#define NVM_PAYLOAD_LEN   (CONFIG_PAYLOAD_LEN + 1U + 2U + ALERT_STATE_HDR_LEN + 1U + PROTO_MSG_MAX)
#define NVM_RECORD_LEN    (NVM_HDR_LEN + NVM_PAYLOAD_LEN + 4U)
#define NVM_SLOTS         (FLASH_PAGE_SIZE / NVM_RECORD_LEN)

/* Defined by STM32WLE5XX_FLASH.ld: last flash page, excluded from ROM */
extern uint8_t __nvm_start[];

_Static_assert((NVM_RECORD_LEN % 8U) == 0U, "records are programmed in double words");
_Static_assert(NVM_SLOTS >= 2U, "at least two records per page");

static uint8_t s_record[NVM_RECORD_LEN] __attribute__((aligned(8)));

static uint32_t slot_addr(uint32_t slot)
{
  return (uint32_t)__nvm_start + (slot * NVM_RECORD_LEN);
}

static uint32_t record_crc(const uint8_t *rec)
{
  return HAL_CRC_Calculate(&hcrc, (uint32_t *)(uintptr_t)rec, NVM_RECORD_LEN - 4U);
}

static bool slot_erased(uint32_t slot)
{
  const uint8_t *p = (const uint8_t *)slot_addr(slot);

  for (uint32_t i = 0; i < NVM_RECORD_LEN; i++)
  {
    if (p[i] != 0xFFU)
    {
      return false;
    }
  }
  return true;
}

static void serialize(const nvm_data_t *in, uint8_t *rec)
{
  uint8_t *p = &rec[NVM_HDR_LEN];

  memset(rec, 0, NVM_RECORD_LEN);
  be_put32(&rec[0], NVM_MAGIC);
  be_put16(&rec[4], NVM_VERSION);
  be_put16(&rec[6], NVM_PAYLOAD_LEN);

  p += proto_build_config(&in->cfg, p, CONFIG_PAYLOAD_LEN);
  *p++ = in->alert_active ? 1U : 0U;
  be_put16(p, in->seq_base);
  p += 2;
  be_put32(p, (uint32_t)in->alert.lat);
  be_put32(p + 4, (uint32_t)in->alert.lon);
  p[8] = in->alert.status;
  p[9] = in->alert.battery;
  p[10] = in->alert.msg_len;
  memcpy(&p[11], in->alert.msg, in->alert.msg_len);

  be_put32(&rec[NVM_RECORD_LEN - 4U], record_crc(rec));
}

static bool deserialize(const uint8_t *rec, nvm_data_t *out)
{
  if ((be_get32(&rec[0]) != NVM_MAGIC) || (be_get16(&rec[4]) != NVM_VERSION)
      || (be_get16(&rec[6]) != NVM_PAYLOAD_LEN)
      || (be_get32(&rec[NVM_RECORD_LEN - 4U]) != record_crc(rec)))
  {
    return false;
  }

  const uint8_t *p = &rec[NVM_HDR_LEN];
  nvm_data_t tmp;
  memset(&tmp, 0, sizeof(tmp));

  /* The config is re-validated: a newer firmware may have tightened limits */
  if (proto_parse_config(p, CONFIG_PAYLOAD_LEN, &tmp.cfg) != RESULT_OK)
  {
    return false;
  }
  p += CONFIG_PAYLOAD_LEN;
  tmp.alert_active = (*p++ != 0U);
  tmp.seq_base = be_get16(p);
  p += 2;
  tmp.alert.lat = (int32_t)be_get32(p);
  tmp.alert.lon = (int32_t)be_get32(p + 4);
  tmp.alert.status = p[8];
  tmp.alert.battery = p[9];
  tmp.alert.msg_len = p[10];
  if ((tmp.alert.msg_len > PROTO_MSG_MAX) || !alert_coords_valid(tmp.alert.lat, tmp.alert.lon))
  {
    return false;
  }
  memcpy(tmp.alert.msg, &p[11], tmp.alert.msg_len);

  *out = tmp;
  return true;
}

bool nvm_load(nvm_data_t *out)
{
  bool found = false;

  for (uint32_t slot = 0; slot < NVM_SLOTS; slot++)
  {
    if (slot_erased(slot))
    {
      break;   /* records are appended in order: the rest is empty */
    }
    memcpy(s_record, (const void *)slot_addr(slot), NVM_RECORD_LEN);
    if (deserialize(s_record, out))
    {
      found = true;
    }
    else
    {
      diag_set_flag(ERR_FLAG_NVM_READ);
    }
  }
  return found;
}

static bool flash_erase_page(void)
{
  FLASH_EraseInitTypeDef erase = {
    .TypeErase = FLASH_TYPEERASE_PAGES,
    .Page = ((uint32_t)__nvm_start - FLASH_BASE) / FLASH_PAGE_SIZE,
    .NbPages = 1U,
  };
  uint32_t page_error = 0;

  return HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
}

static bool flash_write_slot(uint32_t slot, const uint8_t *rec)
{
  uint32_t addr = slot_addr(slot);

  for (uint32_t off = 0; off < NVM_RECORD_LEN; off += 8U)
  {
    uint64_t dw;
    memcpy(&dw, &rec[off], sizeof(dw));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + off, dw) != HAL_OK)
    {
      return false;
    }
  }
  return memcmp((const void *)addr, rec, NVM_RECORD_LEN) == 0;
}

bool nvm_save(const nvm_data_t *in)
{
  bool ok = false;

  serialize(in, s_record);

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    diag_set_flag(ERR_FLAG_NVM_WRITE);
    return false;
  }
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

  /* Next free slot; a failed write burns its slot and we try the next one */
  for (uint32_t slot = 0; (slot < NVM_SLOTS) && !ok; slot++)
  {
    if (slot_erased(slot))
    {
      ok = flash_write_slot(slot, s_record);
    }
  }

  /* Page full (or every remaining slot failed): erase and start over */
  if (!ok && flash_erase_page())
  {
    ok = flash_write_slot(0, s_record);
  }

  (void)HAL_FLASH_Lock();
  if (!ok)
  {
    diag_set_flag(ERR_FLAG_NVM_WRITE);
  }
  return ok;
}
