/**
  ******************************************************************************
  * @file    radio_link.c
  * @brief   Radio task: LoRa configuration, CAD + backoff + TX, continuous RX
  *          and recovery from a stuck radio.
  ******************************************************************************
  */

#include "radio_link.h"

#include <string.h>

#include "app_log.h"
#include "app_random.h"
#include "cmsis_os2.h"
#include "node_diag.h"
#include "radio.h"
#include "radio_driver.h"
#include "subghz_phy_app.h"
#include "supervisor.h"

#define INBOX_DEPTH             8U
#define IDLE_WAIT_MS            1000U   /* also the supervisor check-in period */
#define CAD_TIMEOUT_MS          200U
#define CAD_MAX_RETRIES         5U
#define CAD_BACKOFF_MIN_MS      50U
#define CAD_BACKOFF_MAX_MS      500U
#define TX_DRIVER_TIMEOUT_MS    3000U   /* Radio.SetTxConfig() timeout */
#define TX_WATCHDOG_MS          (TX_DRIVER_TIMEOUT_MS + 1000U)
#define RX_GUARD_PERIOD_MS      30000U
#define MAX_CONSECUTIVE_FAILS   3U

typedef enum
{
  MSG_CMD_CONFIG = 0,
  MSG_CMD_SEND,
  MSG_CMD_RX,
  MSG_CMD_SLEEP,
  MSG_EVT_TX_DONE,
  MSG_EVT_TX_TIMEOUT,
  MSG_EVT_RX_DONE,
  MSG_EVT_RX_TIMEOUT,
  MSG_EVT_RX_ERROR,
  MSG_EVT_CAD_DONE,
} radio_msg_type_t;

typedef struct
{
  uint8_t type;            /* radio_msg_type_t */
  uint8_t busy;            /* CAD result */
  uint8_t len;
  int8_t snr;
  int16_t rssi;
  union
  {
    node_config_t cfg;
    uint8_t data[ALERT_MAX_PACKET];
  } u;
} radio_msg_t;

typedef enum
{
  RS_IDLE = 0,     /* resting: sleep or continuous RX, see s_rest_rx */
  RS_CAD,
  RS_BACKOFF,
  RS_TX,
} radio_state_t;

typedef struct
{
  bool valid;
  uint8_t len;
  uint8_t data[ALERT_MAX_PACKET];
} tx_slot_t;

static osMessageQueueId_t s_inbox;
static radio_report_handler_t s_handler;
static volatile bool s_ready;

static radio_state_t s_state = RS_IDLE;
static bool s_rest_rx;               /* resting mode: true = RX, false = sleep */
static bool s_cfg_valid;
static bool s_cfg_pending;           /* new config to apply once TX ends */
static node_config_t s_cfg;
static node_config_t s_cfg_next;
static tx_slot_t s_tx_current;
static tx_slot_t s_tx_next;          /* one queued TX; a newer one replaces it */
static uint8_t s_cad_tries;
static uint8_t s_fail_count;
static uint32_t s_deadline;
static bool s_deadline_armed;
static uint32_t s_last_rx_guard;

static radio_msg_t s_msg;            /* inbox receive buffer (task only) */
static radio_report_t s_report;      /* report buffer (task only) */

static const osMessageQueueAttr_t s_inbox_attr = { .name = "radioQueue" };

/* ------------------------------------------------------------- posting ---- */

static bool post(const radio_msg_t *msg)
{
  if ((s_inbox == NULL) || (osMessageQueuePut(s_inbox, msg, 0U, 0U) != osOK))
  {
    return false;
  }
  return true;
}

void radio_link_init(radio_report_handler_t handler)
{
  s_handler = handler;
  s_inbox = osMessageQueueNew(INBOX_DEPTH, sizeof(radio_msg_t), &s_inbox_attr);
}

void radio_link_set_ready(void)
{
  s_ready = true;
}

bool radio_link_is_ready(void)
{
  return s_ready;
}

bool radio_link_configure(const node_config_t *cfg)
{
  radio_msg_t msg = { .type = MSG_CMD_CONFIG };
  msg.u.cfg = *cfg;
  return post(&msg);
}

bool radio_link_send(const uint8_t *packet, uint8_t len)
{
  radio_msg_t msg = { .type = MSG_CMD_SEND, .len = len };
  memcpy(msg.u.data, packet, len);
  return post(&msg);
}

bool radio_link_start_rx(void)
{
  radio_msg_t msg = { .type = MSG_CMD_RX };
  return post(&msg);
}

bool radio_link_sleep(void)
{
  radio_msg_t msg = { .type = MSG_CMD_SLEEP };
  return post(&msg);
}

/* ISR context: events only, no Radio calls */
static void post_event(uint8_t type)
{
  radio_msg_t msg = { .type = type };
  (void)post(&msg);
}

void radio_link_on_tx_done(void)
{
  post_event(MSG_EVT_TX_DONE);
}

void radio_link_on_tx_timeout(void)
{
  post_event(MSG_EVT_TX_TIMEOUT);
}

void radio_link_on_rx_timeout(void)
{
  post_event(MSG_EVT_RX_TIMEOUT);
}

void radio_link_on_rx_error(void)
{
  post_event(MSG_EVT_RX_ERROR);
}

void radio_link_on_cad_done(bool channel_busy)
{
  radio_msg_t msg = { .type = MSG_EVT_CAD_DONE, .busy = channel_busy ? 1U : 0U };
  (void)post(&msg);
}

void radio_link_on_rx_done(const uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
  radio_msg_t msg = { .type = MSG_EVT_RX_DONE, .rssi = rssi, .snr = snr };

  msg.len = (size > ALERT_MAX_PACKET) ? (uint8_t)ALERT_MAX_PACKET : (uint8_t)size;
  memcpy(msg.u.data, payload, msg.len);
  (void)post(&msg);
}

/* ---------------------------------------------------------- radio ops ---- */

static void arm_deadline(uint32_t ms)
{
  s_deadline = osKernelGetTickCount() + ms;
  s_deadline_armed = true;
}

static void set_cad_params(uint8_t sf)
{
  /* Semtech AN1200.48 recommendations for BW 125 kHz */
  RadioLoRaCadSymbols_t symbols = (sf <= 8U) ? LORA_CAD_02_SYMBOL : ((sf <= 11U) ? LORA_CAD_04_SYMBOL : LORA_CAD_08_SYMBOL);
  uint8_t det_peak = (uint8_t)(sf + 13U + ((sf >= 12U) ? 3U : 0U));
  SUBGRF_SetCadParams(symbols, det_peak, 10U, LORA_CAD_ONLY, 0U);
}

static void apply_config(const node_config_t *cfg)
{
  s_cfg = *cfg;
  s_cfg_valid = true;

  Radio.SetModem(MODEM_LORA);
  Radio.SetPublicNetwork(false);
  Radio.SetChannel(RADIO_FIXED_FREQ_HZ);
  Radio.SetTxConfig(MODEM_LORA, cfg->tx_power, 0U, cfg->bw, cfg->sf, cfg->cr, cfg->preamble,
                    false, true, false, 0U, false, TX_DRIVER_TIMEOUT_MS);
  Radio.SetRxConfig(MODEM_LORA, cfg->bw, cfg->sf, cfg->cr, 0U, cfg->preamble, 0U,
                    false, 0U, true, false, 0U, false, true);
  Radio.SetMaxPayloadLength(MODEM_LORA, ALERT_MAX_PACKET);
  set_cad_params(cfg->sf);
  APP_LOG("radio: SF%u BW%u CR4/%u %ddBm pre=%u", cfg->sf, cfg->bw, cfg->cr + 4U,
          cfg->tx_power, cfg->preamble);
}

static void go_rest(void)
{
  s_state = RS_IDLE;
  s_deadline_armed = false;

  if (s_rest_rx && s_cfg_valid)
  {
    Radio.RxBoosted(0U);
    s_last_rx_guard = osKernelGetTickCount();
  }
  else
  {
    Radio.Sleep();
  }
}

static void recover_radio(void)
{
  APP_LOG("radio: %u consecutive failures, re-initialising", s_fail_count);
  diag_set_flag(ERR_FLAG_RADIO_RESET);
  s_fail_count = 0;
  SubghzApp_Init();     /* Radio.Init(): resets and reconfigures the radio */
  if (s_cfg_valid)
  {
    apply_config(&s_cfg);
  }
}

static void report(radio_report_type_t type)
{
  s_report.type = type;
  s_report.len = 0;
  if (s_handler != NULL)
  {
    s_handler(&s_report);
  }
}

static void start_cad(void)
{
  s_state = RS_CAD;
  arm_deadline(CAD_TIMEOUT_MS);
  Radio.StartCad();
}

static void start_tx_sequence(void)
{
  s_cad_tries = 0;
  start_cad();
}

static void finish_tx(void);

static void do_send(void)
{
  s_state = RS_TX;
  arm_deadline(TX_WATCHDOG_MS);
  if (Radio.Send(s_tx_current.data, s_tx_current.len) != RADIO_STATUS_OK)
  {
    diag_set_flag(ERR_FLAG_RADIO_TX_TIMEOUT);
    s_fail_count++;
    report(RADIO_REPORT_TX_FAILED);
    finish_tx();
  }
}

static void finish_tx(void)
{
  s_tx_current.valid = false;
  if (s_cfg_pending)
  {
    s_cfg_pending = false;
    apply_config(&s_cfg_next);
  }
  if (s_fail_count >= MAX_CONSECUTIVE_FAILS)
  {
    recover_radio();
  }
  if (s_tx_next.valid)
  {
    s_tx_current = s_tx_next;
    s_tx_next.valid = false;
    start_tx_sequence();
  }
  else
  {
    go_rest();
  }
}

/* ------------------------------------------------------------ handlers ---- */

static void handle_command(const radio_msg_t *msg)
{
  bool tx_active = (s_state != RS_IDLE);

  switch (msg->type)
  {
    case MSG_CMD_CONFIG:
      if (tx_active)
      {
        s_cfg_next = msg->u.cfg;
        s_cfg_pending = true;
      }
      else
      {
        apply_config(&msg->u.cfg);
        go_rest();
      }
      break;

    case MSG_CMD_SEND:
      if (!s_cfg_valid)
      {
        report(RADIO_REPORT_TX_FAILED);
        break;
      }
      if (tx_active)
      {
        s_tx_next.valid = true;
        s_tx_next.len = msg->len;
        memcpy(s_tx_next.data, msg->u.data, msg->len);
      }
      else
      {
        s_tx_current.valid = true;
        s_tx_current.len = msg->len;
        memcpy(s_tx_current.data, msg->u.data, msg->len);
        start_tx_sequence();
      }
      break;

    case MSG_CMD_RX:
    case MSG_CMD_SLEEP:
      s_rest_rx = (msg->type == MSG_CMD_RX);
      if (!tx_active)
      {
        go_rest();
      }
      break;

    default:
      break;
  }
}

static void handle_event(const radio_msg_t *msg)
{
  switch (msg->type)
  {
    case MSG_EVT_CAD_DONE:
      if (s_state != RS_CAD)
      {
        break;
      }
      if ((msg->busy != 0U) && (s_cad_tries < CAD_MAX_RETRIES))
      {
        s_cad_tries++;
        s_state = RS_BACKOFF;
        Radio.Standby();
        arm_deadline(app_random_range(CAD_BACKOFF_MIN_MS, CAD_BACKOFF_MAX_MS));
      }
      else
      {
        if (msg->busy != 0U)
        {
          diag_set_flag(ERR_FLAG_CAD_BUSY);   /* alert has priority: send anyway */
        }
        do_send();
      }
      break;

    case MSG_EVT_TX_DONE:
      if (s_state == RS_TX)
      {
        s_fail_count = 0;
        report(RADIO_REPORT_TX_DONE);
        finish_tx();
      }
      break;

    case MSG_EVT_TX_TIMEOUT:
      if (s_state == RS_TX)
      {
        diag_set_flag(ERR_FLAG_RADIO_TX_TIMEOUT);
        s_fail_count++;
        report(RADIO_REPORT_TX_FAILED);
        finish_tx();
      }
      break;

    case MSG_EVT_RX_DONE:
      if ((s_state == RS_IDLE) && s_rest_rx && (s_handler != NULL))
      {
        s_report.type = RADIO_REPORT_RX;
        s_report.rssi = msg->rssi;
        s_report.snr = msg->snr;
        s_report.len = msg->len;
        memcpy(s_report.data, msg->u.data, msg->len);
        s_handler(&s_report);
      }
      break;

    case MSG_EVT_RX_TIMEOUT:
    case MSG_EVT_RX_ERROR:
      /* Continuous RX normally stays armed; re-arm to be sure */
      if ((s_state == RS_IDLE) && s_rest_rx)
      {
        go_rest();
      }
      break;

    default:
      break;
  }
}

static void handle_deadline(void)
{
  s_deadline_armed = false;

  switch (s_state)
  {
    case RS_CAD:
      /* No CadDone: count it and transmit without LBT */
      s_fail_count++;
      do_send();
      break;

    case RS_BACKOFF:
      start_cad();
      break;

    case RS_TX:
      /* Neither TxDone nor TxTimeout from the driver */
      diag_set_flag(ERR_FLAG_RADIO_TX_TIMEOUT);
      s_fail_count = MAX_CONSECUTIVE_FAILS;
      report(RADIO_REPORT_TX_FAILED);
      finish_tx();
      break;

    default:
      break;
  }
}

static void rx_guard(uint32_t now)
{
  if ((s_state != RS_IDLE) || !s_rest_rx || !s_cfg_valid || ((now - s_last_rx_guard) < RX_GUARD_PERIOD_MS))
  {
    return;
  }
  s_last_rx_guard = now;
  if (Radio.GetStatus() != RF_RX_RUNNING)
  {
    APP_LOG("radio: RX not running, re-arming");
    go_rest();
  }
}

void radio_link_task(void)
{
  /* Commands queue up until the SubGHz_Phy init in the default task is done */
  while (!s_ready)
  {
    supervisor_checkin(SUP_TASK_RADIO);
    osDelay(10U);
  }

  for (;;)
  {
    uint32_t now = osKernelGetTickCount();
    uint32_t wait = IDLE_WAIT_MS;

    if (s_deadline_armed)
    {
      int32_t left = (int32_t)(s_deadline - now);
      wait = (left <= 0) ? 0U : (((uint32_t)left < IDLE_WAIT_MS) ? (uint32_t)left : IDLE_WAIT_MS);
    }

    osStatus_t st = osMessageQueueGet(s_inbox, &s_msg, NULL, wait);
    supervisor_checkin(SUP_TASK_RADIO);

    if (st == osOK)
    {
      if (s_msg.type <= MSG_CMD_SLEEP)
      {
        handle_command(&s_msg);
      }
      else
      {
        handle_event(&s_msg);
      }
    }

    now = osKernelGetTickCount();
    if (s_deadline_armed && ((int32_t)(now - s_deadline) >= 0))
    {
      handle_deadline();
    }
    rx_guard(now);
  }
}
