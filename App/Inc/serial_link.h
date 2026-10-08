/**
  ******************************************************************************
  * @file    serial_link.h
  * @brief   USART2 <-> Android app link. The UART task is the only owner of
  *          huart2 once the scheduler runs:
  *          - RX: circular DMA + idle line detection, bytes go through the
  *            frame decoder and complete frames are handed to the node task;
  *          - TX: other tasks enqueue frames, the UART task sends them by DMA.
  ******************************************************************************
  */

#ifndef SERIAL_LINK_H
#define SERIAL_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* Received, CRC-checked frame. Called from the UART task. */
typedef void (*serial_frame_handler_t)(uint8_t type, const uint8_t *payload, uint8_t len);

/* Creates the TX queue. Call before the scheduler starts. */
void serial_link_init(serial_frame_handler_t handler);

/* Enqueues one frame (non-blocking). Returns false and flags
 * ERR_FLAG_SERIAL_TX_DROP if the queue is full. Task context only. */
bool serial_link_send(uint8_t type, const uint8_t *payload, uint8_t len);

/* Blocking send for use before the scheduler starts. */
void serial_link_send_early(uint8_t type, const uint8_t *payload, uint8_t len);

/* Waits until every queued frame has been sent, up to timeout_ms. */
bool serial_link_flush(uint32_t timeout_ms);

/* UART task body. */
void serial_link_task(void);

#endif /* SERIAL_LINK_H */
