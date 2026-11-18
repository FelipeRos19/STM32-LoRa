/**
  ******************************************************************************
  * @file    node_diag.h
  * @brief   Diagnostics shared by every module: sticky error flags, reset
  *          cause, fault record and the bootloader request that survives a
  *          software reset (kept in SRAM2, which startup code does not clear).
  ******************************************************************************
  */

#ifndef NODE_DIAG_H
#define NODE_DIAG_H

#include <stdint.h>

#include "proto_defs.h"

/* Fault codes recorded before a forced reset */
#define DIAG_FAULT_NONE            0U
#define DIAG_FAULT_STACK_OVERFLOW  1U
#define DIAG_FAULT_MALLOC          2U
#define DIAG_FAULT_ERROR_HANDLER   3U
#define DIAG_FAULT_HARDFAULT       4U

/* Must run first in main(), before HAL_Init(): jumps to the ST system
 * bootloader if the previous run asked for it, and latches the reset cause. */
void diag_early_boot(void);

reset_cause_t diag_reset_cause(void);

/* Fault left by the previous run (DIAG_FAULT_x), cleared after reading. */
uint32_t diag_take_last_fault(char *task_name, uint32_t name_size);

/* Sticky error flags (ERR_FLAG_x), safe from tasks and ISRs. */
void diag_set_flag(uint16_t flag);
uint16_t diag_flags(void);

/* Records a fault and resets the MCU. */
void diag_fault_reset(uint32_t code, const char *task_name);

/* Requests the ST system bootloader and resets; diag_early_boot() jumps to it. */
void diag_enter_bootloader(void);

#endif /* NODE_DIAG_H */
