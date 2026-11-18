/**
  ******************************************************************************
  * @file    node_app.c
  * @brief   Node state machine. Owns the configuration, the alert state, the
  *          sequence number and the persisted record; only this task writes
  *          them.
  ******************************************************************************
  */

#include "node_app.h"

#include <string.h>

#include "alert_packet.h"
#include "app_log.h"
#include "app_random.h"
#include "cmsis_os2.h"
#include "node_diag.h"
#include "nvm_store.h"
#include "proto_frame.h"
#include "proto_msgs.h"
#include "serial_link.h"
#include "stm32_timer.h"
#include "supervisor.h"

#define INBOX_DEPTH              6U
#define LOOP_PERIOD_MS           250U
#define HEARTBEAT_PERIOD_MS      1000U
#define WARMUP_WINDOW_MS         5000U
#define TX_MIN_GAP_MS            10000U
#define TX_RETRY_MS              30000U
#define TX_BUSY_RETRY_MS         1000U
#define JITTER_PERCENT           10U
#define POSITION_SAVE_PERIOD_MS  (5U * 60U * 1000U)
#define DEDUP_ENTRIES            32U
#define DEDUP_WINDOW_MS          (10U * 60U * 1000U)
#define BOOTLOADER_FLUSH_MS      300U

typedef enum
{
  NODE_MSG_FRAME = 0,
  NODE_MSG_RADIO,
  NODE_MSG_BEACON_TIMER,
} node_msg_kind_t;

typedef struct
{
  uint8_t kind;          /* node_msg_kind_t */
  uint8_t type;          /* frame type or radio_report_type_t */
  uint8_t len;
  int8_t snr;
  int16_t rssi;
  uint8_t data[ALERT_MAX_PACKET];
} node_msg_t;

typedef struct
{
  uint32_t user_id;
  uint16_t seq;
  uint32_t tick;
  bool used;
} dedup_entry_t;

static osMessageQueueId_t s_inbox;
static const osMessageQueueAttr_t s_inbox_attr = { .name = "nodeQueue" };

static node_state_t s_state = NODE_STATE_WAIT_CONFIG;
static node_config_t s_cfg;
static bool s_configured;
static alert_state_t s_alert;
static bool s_alert_active;
static uint16_t s_seq;
static uint16_t s_seq_base;
static uint16_t s_last_seq;

static UTIL_TIMER_Object_t s_beacon_timer;
static bool s_tx_in_flight;
static bool s_tx_requested;          /* TX asked for while another was in flight */
static bool s_has_tx;
static uint32_t s_last_tx_tick;

static uint32_t s_boot_tick;
static bool s_app_seen;
static uint32_t s_last_heartbeat;
static bool s_position_dirty;
static uint32_t s_last_position_save;

static dedup_entry_t s_dedup[DEDUP_ENTRIES];
static uint8_t s_dedup_next;

/* Large buffers kept static to keep the task stack small */
static node_msg_t s_msg;
static nvm_data_t s_nvm;
static alert_packet_t s_packet;
static uint8_t s_buf[PROTO_MAX_PAYLOAD + 5U];

/* ------------------------------------------------------------- inbox ------ */

void node_app_on_frame(uint8_t type, const uint8_t *payload, uint8_t len)
{
  node_msg_t msg = { .kind = NODE_MSG_FRAME, .type = type, .len = len };

  memcpy(msg.data, payload, len);
  (void)osMessageQueuePut(s_inbox, &msg, 0U, 0U);   /* full: the app retries on ACK timeout */
}

void node_app_on_radio(const radio_report_t *report)
{
  node_msg_t msg = { .kind = NODE_MSG_RADIO, .type = (uint8_t)report->type, .len = report->len,
                     .rssi = report->rssi, .snr = report->snr };

  memcpy(msg.data, report->data, report->len);
  if (osMessageQueuePut(s_inbox, &msg, 0U, 0U) != osOK)
  {
    APP_LOG("node: inbox full, radio report dropped");
  }
}

/* RTC alarm (ISR) context */
static void beacon_timer_cb(void *arg)
{
  (void)arg;
  node_msg_t msg = { .kind = NODE_MSG_BEACON_TIMER };
  (void)osMessageQueuePut(s_inbox, &msg, 0U, 0U);
}

void node_app_init(void)
{
  s_inbox = osMessageQueueNew(INBOX_DEPTH, sizeof(node_msg_t), &s_inbox_attr);
  serial_link_init(node_app_on_frame);
  radio_link_init(node_app_on_radio);
}

/* ------------------------------------------------------------ helpers ----- */

static uint8_t max_msg_len(void)
{
  return s_configured ? node_config_max_msg(&s_cfg) : 0U;
}

static void send_status(uint8_t ack_type, proto_result_t result)
{
  node_status_t st = {
    .ack_type = ack_type,
    .result = (uint8_t)result,
    .state = (uint8_t)s_state,
    .role = s_configured ? s_cfg.role : (uint8_t)NODE_ROLE_NONE,
    .max_msg_len = max_msg_len(),
    .last_seq = s_last_seq,
    .err_flags = diag_flags(),
    .reset_cause = (uint8_t)diag_reset_cause(),
  };
  size_t n = proto_build_status(&st, s_buf, sizeof(s_buf));
  (void)serial_link_send(FRAME_STATUS, s_buf, (uint8_t)n);
}

static bool persist(void)
{
  s_nvm.cfg = s_cfg;
  s_nvm.alert_active = s_alert_active;
  s_nvm.seq_base = s_seq_base;
  s_nvm.alert = s_alert;
  bool ok = nvm_save(&s_nvm);
  if (ok)
  {
    s_position_dirty = false;
    s_last_position_save = osKernelGetTickCount();
  }
  return ok;
}

static void beacon_timer_start(uint32_t delay_ms)
{
  (void)UTIL_TIMER_Stop(&s_beacon_timer);
  (void)UTIL_TIMER_StartWithPeriod(&s_beacon_timer, (delay_ms > 0U) ? delay_ms : 1U);
}

static void schedule_periodic(void)
{
  uint32_t interval_ms = (uint32_t)s_cfg.interval_s * 1000U;
  uint32_t jitter = (interval_ms * JITTER_PERCENT) / 100U;

  beacon_timer_start(app_random_range(interval_ms - jitter, interval_ms + jitter));
}

/* ------------------------------------------------------- transmission ----- */

static void transmit(void)
{
  /* Keep every used seq below the persisted base + block (see nvm_store.h) */
  if ((uint16_t)(s_seq - s_seq_base) >= NVM_SEQ_BLOCK)
  {
    s_seq_base = s_seq;
    (void)persist();
  }

  s_packet.port = ALERT_PORT_ALERT;
  s_packet.user_id = s_cfg.user_id;
  s_packet.seq = s_seq;
  s_packet.lat = s_alert.lat;
  s_packet.lon = s_alert.lon;
  s_packet.status = s_alert.status;
  s_packet.battery = s_alert.battery;

  /* A later CONFIG may have shrunk the allowed message: truncate safely */
  uint8_t max_msg = max_msg_len();
  s_packet.msg_len = (s_alert.msg_len > max_msg) ? max_msg : s_alert.msg_len;
  memcpy(s_packet.msg, s_alert.msg, s_packet.msg_len);
  alert_msg_sanitize_utf8(s_packet.msg, s_packet.msg_len);

  size_t n = alert_packet_encode(&s_packet, s_buf, sizeof(s_buf));
  if ((n == 0U) || !radio_link_send(s_buf, (uint8_t)n))
  {
    send_status(ACK_EVENT, RESULT_E_BUSY);
    beacon_timer_start(TX_BUSY_RETRY_MS);
    return;
  }

  APP_LOG("node: TX seq=%u status=%u msg=%u", s_seq, s_alert.status, s_packet.msg_len);
  s_tx_requested = false;
  s_last_seq = s_seq;
  s_seq++;
  s_tx_in_flight = true;
  s_has_tx = true;
  s_last_tx_tick = osKernelGetTickCount();
  (void)UTIL_TIMER_Stop(&s_beacon_timer);   /* rescheduled when the TX completes */
}

/* Transmits now, or as soon as the minimum gap since the last TX allows */
static void request_tx(bool respect_gap)
{
  uint32_t since = osKernelGetTickCount() - s_last_tx_tick;

  if (respect_gap && s_has_tx && (since < TX_MIN_GAP_MS))
  {
    beacon_timer_start(TX_MIN_GAP_MS - since);
    return;
  }
  transmit();
}

static void stop_alert(bool send_safe_packet)
{
  if (!s_alert_active)
  {
    return;
  }
  (void)UTIL_TIMER_Stop(&s_beacon_timer);
  if (send_safe_packet)
  {
    s_alert.status = ALERT_STATUS_SAFE;
    transmit();
  }
  s_alert_active = false;
  if (s_state == NODE_STATE_BEACON_ALERT)
  {
    s_state = NODE_STATE_BEACON_IDLE;
  }
}

/* --------------------------------------------------------- role/state ----- */

static void enter_role_state(void)
{
  if (s_cfg.role == NODE_ROLE_SNIFFER)
  {
    s_state = NODE_STATE_SNIFFER_RX;
    (void)radio_link_start_rx();
  }
  else
  {
    s_state = s_alert_active ? NODE_STATE_BEACON_ALERT : NODE_STATE_BEACON_IDLE;
    (void)radio_link_sleep();
  }
}

static bool is_beacon_state(void)
{
  return (s_state == NODE_STATE_BEACON_IDLE) || (s_state == NODE_STATE_BEACON_ALERT);
}

/* --------------------------------------------------------- app frames ----- */

static void on_config(const uint8_t *payload, uint8_t len)
{
  node_config_t cfg;
  proto_result_t res = proto_parse_config(payload, len, &cfg);

  if (res != RESULT_OK)
  {
    send_status(FRAME_CONFIG, res);
    return;
  }

  bool role_changed = !s_configured || (cfg.role != s_cfg.role);
  if (role_changed)
  {
    stop_alert(false);
  }
  s_cfg = cfg;
  s_configured = true;
  (void)radio_link_configure(&s_cfg);
  enter_role_state();

  bool saved = persist();
  APP_LOG("node: CONFIG role=%u user=%08lX SF%u -> max_msg=%u", s_cfg.role,
          (unsigned long)s_cfg.user_id, s_cfg.sf, max_msg_len());
  send_status(FRAME_CONFIG, saved ? RESULT_OK : RESULT_E_FLASH);
}

static void on_alert_start(const uint8_t *payload, uint8_t len)
{
  alert_state_t st;

  if (!is_beacon_state())
  {
    send_status(FRAME_ALERT_START, RESULT_E_STATE);
    return;
  }
  proto_result_t res = proto_parse_alert_state(payload, len, max_msg_len(), &st);
  if (res != RESULT_OK)
  {
    send_status(FRAME_ALERT_START, res);
    return;
  }

  s_alert = st;
  s_alert_active = true;
  s_state = NODE_STATE_BEACON_ALERT;
  bool saved = persist();
  send_status(FRAME_ALERT_START, saved ? RESULT_OK : RESULT_E_FLASH);
  request_tx(true);
}

static void on_update(const uint8_t *payload, uint8_t len)
{
  alert_state_t st;

  if (!is_beacon_state())
  {
    send_status(FRAME_UPDATE, RESULT_E_STATE);
    return;
  }
  proto_result_t res = proto_parse_alert_state(payload, len, max_msg_len(), &st);
  if (res != RESULT_OK)
  {
    send_status(FRAME_UPDATE, res);
    return;
  }

  bool changed = (st.status != s_alert.status) || (st.msg_len != s_alert.msg_len)
                 || (memcmp(st.msg, s_alert.msg, st.msg_len) != 0);
  s_alert = st;

  bool saved = true;
  if (changed)
  {
    saved = persist();
  }
  else
  {
    s_position_dirty = true;   /* saved by housekeeping, limits flash wear */
  }
  send_status(FRAME_UPDATE, saved ? RESULT_OK : RESULT_E_FLASH);

  if (changed && s_alert_active)
  {
    request_tx(true);
  }
}

static void on_alert_stop(void)
{
  if (!is_beacon_state())
  {
    send_status(FRAME_ALERT_STOP, RESULT_E_STATE);
    return;
  }
  bool saved = true;
  if (s_alert_active)
  {
    stop_alert(true);
    saved = persist();
  }
  send_status(FRAME_ALERT_STOP, saved ? RESULT_OK : RESULT_E_FLASH);
}

static void on_enter_bootloader(const uint8_t *payload, uint8_t len)
{
  if ((len != BOOTLOADER_MAGIC_LEN) || (memcmp(payload, BOOTLOADER_MAGIC, BOOTLOADER_MAGIC_LEN) != 0))
  {
    send_status(FRAME_ENTER_BOOTLOADER, RESULT_E_PARAM);
    return;
  }
  send_status(FRAME_ENTER_BOOTLOADER, RESULT_OK);
  (void)serial_link_flush(BOOTLOADER_FLUSH_MS);
  diag_enter_bootloader();
}

static void handle_frame(const node_msg_t *msg)
{
  s_app_seen = true;

  switch (msg->type)
  {
    case FRAME_CONFIG:
      on_config(msg->data, msg->len);
      break;
    case FRAME_ALERT_START:
      on_alert_start(msg->data, msg->len);
      break;
    case FRAME_UPDATE:
      on_update(msg->data, msg->len);
      break;
    case FRAME_ALERT_STOP:
      on_alert_stop();
      break;
    case FRAME_GET_STATUS:
      send_status(FRAME_GET_STATUS, RESULT_OK);
      break;
    case FRAME_ENTER_BOOTLOADER:
      on_enter_bootloader(msg->data, msg->len);
      break;
    default:
      send_status(msg->type, RESULT_E_TYPE);
      break;
  }
}

/* ------------------------------------------------------- radio events ----- */

static bool dedup_seen(uint32_t user_id, uint16_t seq, uint32_t now)
{
  for (uint32_t i = 0; i < DEDUP_ENTRIES; i++)
  {
    if (s_dedup[i].used && (s_dedup[i].user_id == user_id) && (s_dedup[i].seq == seq)
        && ((now - s_dedup[i].tick) < DEDUP_WINDOW_MS))
    {
      return true;
    }
  }
  s_dedup[s_dedup_next] = (dedup_entry_t){ .user_id = user_id, .seq = seq, .tick = now, .used = true };
  s_dedup_next = (uint8_t)((s_dedup_next + 1U) % DEDUP_ENTRIES);
  return false;
}

static void on_radio_rx(const node_msg_t *msg)
{
  /* Beacons ignore alert traffic from other users (the radio is not even in RX) */
  if (s_state != NODE_STATE_SNIFFER_RX)
  {
    return;
  }
  if (!alert_packet_decode(msg->data, msg->len, &s_packet) || (s_packet.port != ALERT_PORT_ALERT)
      || !alert_coords_valid(s_packet.lat, s_packet.lon))
  {
    APP_LOG("node: RX dropped (len=%u port=%u)", msg->len, (msg->len > 0U) ? msg->data[0] : 0U);
    return;
  }
  if (dedup_seen(s_packet.user_id, s_packet.seq, osKernelGetTickCount()))
  {
    return;
  }

  alert_msg_sanitize_utf8(s_packet.msg, s_packet.msg_len);
  size_t n = proto_build_rx_alert(&s_packet, msg->rssi, msg->snr, s_buf, sizeof(s_buf));
  (void)serial_link_send(FRAME_RX_ALERT, s_buf, (uint8_t)n);
}

static void handle_radio(const node_msg_t *msg)
{
  switch ((radio_report_type_t)msg->type)
  {
    case RADIO_REPORT_TX_DONE:
    case RADIO_REPORT_TX_FAILED:
    {
      bool ok = (msg->type == (uint8_t)RADIO_REPORT_TX_DONE);
      s_tx_in_flight = false;
      send_status(ACK_EVENT, ok ? RESULT_OK : RESULT_E_RADIO);
      if (s_alert_active && s_tx_requested)
      {
        request_tx(true);   /* a change arrived during the previous TX */
      }
      else if (s_alert_active)
      {
        if (ok)
        {
          schedule_periodic();
        }
        else
        {
          uint32_t interval_ms = (uint32_t)s_cfg.interval_s * 1000U;
          beacon_timer_start((interval_ms < TX_RETRY_MS) ? interval_ms : TX_RETRY_MS);
        }
      }
      break;
    }

    case RADIO_REPORT_RX:
      on_radio_rx(msg);
      break;

    default:
      break;
  }
}

static void on_beacon_timer(void)
{
  if (!s_alert_active || (s_state != NODE_STATE_BEACON_ALERT))
  {
    return;
  }
  if (s_tx_in_flight)
  {
    s_tx_requested = true;   /* served when the current TX completes */
    return;
  }
  transmit();
}

/* --------------------------------------------------------------- boot ----- */

static void boot_restore(void)
{
  char task[16] = { 0 };
  uint32_t fault = diag_take_last_fault(task, sizeof(task));
  if (fault != DIAG_FAULT_NONE)
  {
    APP_LOG("node: previous run faulted (code %lu, task %s)", (unsigned long)fault, task);
  }

  if (!nvm_load(&s_nvm))
  {
    node_config_defaults(&s_cfg);
    s_state = NODE_STATE_WAIT_CONFIG;
    (void)radio_link_sleep();
    APP_LOG("node: no stored config, waiting for CONFIG");
    return;
  }

  s_cfg = s_nvm.cfg;
  s_configured = true;
  s_alert = s_nvm.alert;
  s_alert_active = (s_cfg.role == NODE_ROLE_BEACON) && s_nvm.alert_active;
  /* Skip the block that may have been used after the last save */
  s_seq_base = (uint16_t)(s_nvm.seq_base + NVM_SEQ_BLOCK);
  s_seq = s_seq_base;
  s_last_seq = (uint16_t)(s_seq - 1U);

  (void)radio_link_configure(&s_cfg);
  enter_role_state();
  if (s_cfg.role == NODE_ROLE_BEACON)
  {
    (void)persist();   /* store the advanced seq base before any TX */
  }
  APP_LOG("node: restored role=%u alert=%u seq=%u", s_cfg.role, s_alert_active ? 1U : 0U, s_seq);

  if (s_alert_active)
  {
    transmit();   /* resume the alert right away */
  }
}

static void housekeeping(uint32_t now)
{
  bool warmup = (s_state == NODE_STATE_WAIT_CONFIG) || ((now - s_boot_tick) < WARMUP_WINDOW_MS);

  if (!s_app_seen && warmup && ((now - s_last_heartbeat) >= HEARTBEAT_PERIOD_MS))
  {
    s_last_heartbeat = now;
    send_status(ACK_EVENT, RESULT_OK);
  }

  if (s_position_dirty && s_configured && ((now - s_last_position_save) >= POSITION_SAVE_PERIOD_MS))
  {
    (void)persist();
  }
}

void node_app_task(void)
{
  /* SubGHz_Phy init (default task) also brings up the RTC timer server */
  while (!radio_link_is_ready())
  {
    supervisor_checkin(SUP_TASK_NODE);
    osDelay(10U);
  }

  UTIL_TIMER_Create(&s_beacon_timer, 1000U, UTIL_TIMER_ONESHOT, beacon_timer_cb, NULL);

  s_boot_tick = osKernelGetTickCount();
  s_last_position_save = s_boot_tick;
  boot_restore();

  s_last_heartbeat = osKernelGetTickCount();
  send_status(ACK_EVENT, RESULT_OK);

  for (;;)
  {
    osStatus_t st = osMessageQueueGet(s_inbox, &s_msg, NULL, LOOP_PERIOD_MS);
    supervisor_checkin(SUP_TASK_NODE);

    if (st == osOK)
    {
      switch (s_msg.kind)
      {
        case NODE_MSG_FRAME:
          handle_frame(&s_msg);
          break;
        case NODE_MSG_RADIO:
          handle_radio(&s_msg);
          break;
        case NODE_MSG_BEACON_TIMER:
          on_beacon_timer();
          break;
        default:
          break;
      }
    }
    housekeeping(osKernelGetTickCount());
  }
}
