/**
  ******************************************************************************
  * @file    radio_link.h
  * @brief   The radio task is the only caller of the SubGHz_Phy Radio API.
  *          Other tasks send it commands; the radio callbacks (ISR context)
  *          only post events. Transmissions use listen-before-talk (CAD with
  *          random backoff) and every operation is supervised by a deadline.
  ******************************************************************************
  */

#ifndef RADIO_LINK_H
#define RADIO_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "proto_msgs.h"

/* Fixed channel until frequency hopping (AU915 sub-band 2, channel 8) */
#define RADIO_FIXED_FREQ_HZ   916800000UL

typedef enum
{
  RADIO_REPORT_TX_DONE = 0,
  RADIO_REPORT_TX_FAILED,
  RADIO_REPORT_RX,
} radio_report_type_t;

typedef struct
{
  radio_report_type_t type;
  int16_t rssi;
  int8_t snr;
  uint8_t len;
  uint8_t data[ALERT_MAX_PACKET];
} radio_report_t;

/* Called from the radio task for every report; must not block. */
typedef void (*radio_report_handler_t)(const radio_report_t *report);

/* Creates the radio inbox. Call before the scheduler starts. */
void radio_link_init(radio_report_handler_t handler);

/* Called by SubghzApp_Init() once Radio.Init() is done. The timer server
 * (UTIL_TIMER) is also initialised by then. */
void radio_link_set_ready(void);
bool radio_link_is_ready(void);

/* Commands (any task). Return false if the inbox is full. */
bool radio_link_configure(const node_config_t *cfg);
bool radio_link_send(const uint8_t *packet, uint8_t len);
bool radio_link_start_rx(void);
bool radio_link_sleep(void);

/* Radio callbacks, wired in Src/subghz_phy_app.c (ISR context). */
void radio_link_on_tx_done(void);
void radio_link_on_tx_timeout(void);
void radio_link_on_rx_done(const uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void radio_link_on_rx_timeout(void);
void radio_link_on_rx_error(void);
void radio_link_on_cad_done(bool channel_busy);

/* Radio task body. */
void radio_link_task(void);

#endif /* RADIO_LINK_H */
