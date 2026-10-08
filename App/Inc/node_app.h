/**
  ******************************************************************************
  * @file    node_app.h
  * @brief   Node state machine (WAIT_CONFIG / BEACON_IDLE / BEACON_ALERT /
  *          SNIFFER_RX): app commands, warmup STATUS heartbeat, beacon
  *          scheduling on the RTC timer server, persistence and RX_ALERT.
  ******************************************************************************
  */

#ifndef NODE_APP_H
#define NODE_APP_H

#include <stdint.h>

#include "radio_link.h"

/* Creates the node inbox and wires serial/radio handlers. Before the scheduler. */
void node_app_init(void);

/* Node task body. */
void node_app_task(void);

/* Handlers given to serial_link / radio_link (called from their tasks). */
void node_app_on_frame(uint8_t type, const uint8_t *payload, uint8_t len);
void node_app_on_radio(const radio_report_t *report);

#endif /* NODE_APP_H */
