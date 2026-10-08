/**
  ******************************************************************************
  * @file    rtos_heap.c
  * @brief   FreeRTOS heap (configAPPLICATION_ALLOCATED_HEAP) placed in SRAM2.
  *          The section is NOLOAD; heap_4 initialises its own structures.
  ******************************************************************************
  */

#include "FreeRTOS.h"

uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".RAM2_region"), aligned(8)));
