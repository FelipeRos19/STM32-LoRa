/**
  ******************************************************************************
  * @file    nvm_store.h
  * @brief   Node configuration and alert state persisted in the last flash
  *          page (NVM region of the linker script).
  *
  *          Records are appended (272 bytes each, 7 per 2 KB page) and the
  *          page is only erased when full; the newest valid record wins.
  *          Every record carries a CRC-32 and is read back after writing.
  ******************************************************************************
  */

#ifndef NVM_STORE_H
#define NVM_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "proto_msgs.h"

typedef struct
{
  node_config_t cfg;
  bool alert_active;
  uint16_t seq_base;      /* every seq used so far is below seq_base + NVM_SEQ_BLOCK */
  alert_state_t alert;
} nvm_data_t;

#define NVM_SEQ_BLOCK  16U

/* Loads the newest valid record. Returns false (and leaves out untouched)
 * if there is none or the stored configuration no longer validates. */
bool nvm_load(nvm_data_t *out);

/* Appends a record. Returns false on flash failure (ERR_FLAG_NVM_WRITE set). */
bool nvm_save(const nvm_data_t *in);

#endif /* NVM_STORE_H */
