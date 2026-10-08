/**
  ******************************************************************************
  * @file    serial_link.c
  * @brief   USART2 framing link (DMA RX ring + queued DMA TX).
  ******************************************************************************
  */

#include "serial_link.h"

#include "cmsis_os2.h"
#include "node_diag.h"
#include "proto_frame.h"
#include "supervisor.h"
#include "usart.h"

#define RX_DMA_SIZE        512U
#define TX_QUEUE_DEPTH     8U
#define TX_TIMEOUT_MS      200U
#define TASK_PERIOD_MS     10U

#define FLAG_RX            (1UL << 0)
#define FLAG_RX_ERROR      (1UL << 1)
#define FLAG_TX_DONE       (1UL << 2)
#define FLAG_TX_PENDING    (1UL << 3)
#define FLAG_ALL           (FLAG_RX | FLAG_RX_ERROR | FLAG_TX_DONE | FLAG_TX_PENDING)

typedef struct
{
  uint16_t len;
  uint8_t data[PROTO_MAX_FRAME];
} tx_item_t;

static uint8_t s_rx_dma[RX_DMA_SIZE];
static uint16_t s_rx_tail;
static proto_decoder_t s_decoder;
static serial_frame_handler_t s_handler;

static osMessageQueueId_t s_tx_queue;
static osThreadId_t s_task;
static tx_item_t s_tx_current;          /* buffer read by the TX DMA */
static volatile bool s_tx_busy;
static uint32_t s_tx_start_ms;

static const osMessageQueueAttr_t s_tx_queue_attr = { .name = "serialTxQueue" };

void serial_link_init(serial_frame_handler_t handler)
{
  s_handler = handler;
  s_tx_queue = osMessageQueueNew(TX_QUEUE_DEPTH, sizeof(tx_item_t), &s_tx_queue_attr);
}

static void notify_task(uint32_t flags)
{
  if (s_task != NULL)
  {
    (void)osThreadFlagsSet(s_task, flags);
  }
}

bool serial_link_send(uint8_t type, const uint8_t *payload, uint8_t len)
{
  tx_item_t item;

  item.len = (uint16_t)proto_frame_encode(type, payload, len, item.data, sizeof(item.data));
  if ((item.len == 0U) || (s_tx_queue == NULL) || (osMessageQueuePut(s_tx_queue, &item, 0U, 0U) != osOK))
  {
    diag_set_flag(ERR_FLAG_SERIAL_TX_DROP);
    return false;
  }
  notify_task(FLAG_TX_PENDING);
  return true;
}

void serial_link_send_early(uint8_t type, const uint8_t *payload, uint8_t len)
{
  static uint8_t buf[PROTO_MAX_FRAME];
  size_t n = proto_frame_encode(type, payload, len, buf, sizeof(buf));

  if (n > 0U)
  {
    (void)HAL_UART_Transmit(&huart2, buf, (uint16_t)n, 100U);
  }
}

bool serial_link_flush(uint32_t timeout_ms)
{
  uint32_t start = osKernelGetTickCount();

  while ((osMessageQueueGetCount(s_tx_queue) > 0U) || s_tx_busy)
  {
    if ((osKernelGetTickCount() - start) > timeout_ms)
    {
      return false;
    }
    osDelay(2U);
  }
  return true;
}

static void frame_cb(void *ctx, proto_evt_t evt, uint8_t type, const uint8_t *payload, uint8_t len)
{
  (void)ctx;

  if (evt == PROTO_EVT_FRAME)
  {
    if (s_handler != NULL)
    {
      s_handler(type, payload, len);
    }
  }
  else if ((evt == PROTO_EVT_ERR_CRC) || (evt == PROTO_EVT_ERR_LEN))
  {
    diag_set_flag(ERR_FLAG_SERIAL_CRC);
  }
}

static void rx_start(void)
{
  s_rx_tail = 0U;
  if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, s_rx_dma, RX_DMA_SIZE) != HAL_OK)
  {
    /* Retried on the next RX error / task cycle */
    diag_set_flag(ERR_FLAG_SERIAL_OVERFLOW);
  }
}

static void rx_drain(uint32_t now_ms)
{
  uint16_t head = (uint16_t)(RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx));

  if (head >= RX_DMA_SIZE)
  {
    head = 0U;
  }
  if (head == s_rx_tail)
  {
    return;
  }

  if (head > s_rx_tail)
  {
    proto_decoder_feed(&s_decoder, &s_rx_dma[s_rx_tail], (size_t)head - s_rx_tail, now_ms, frame_cb, NULL);
  }
  else
  {
    proto_decoder_feed(&s_decoder, &s_rx_dma[s_rx_tail], (size_t)RX_DMA_SIZE - s_rx_tail, now_ms, frame_cb, NULL);
    proto_decoder_feed(&s_decoder, s_rx_dma, head, now_ms, frame_cb, NULL);
  }
  s_rx_tail = head;
}

static void tx_service(uint32_t now_ms)
{
  if (s_tx_busy)
  {
    if ((now_ms - s_tx_start_ms) <= TX_TIMEOUT_MS)
    {
      return;
    }
    /* Stuck transfer: abort and move on */
    (void)HAL_UART_AbortTransmit(&huart2);
    s_tx_busy = false;
    diag_set_flag(ERR_FLAG_SERIAL_TX_DROP);
  }

  if (osMessageQueueGet(s_tx_queue, &s_tx_current, NULL, 0U) == osOK)
  {
    s_tx_busy = true;
    s_tx_start_ms = now_ms;
    if (HAL_UART_Transmit_DMA(&huart2, s_tx_current.data, s_tx_current.len) != HAL_OK)
    {
      s_tx_busy = false;
      diag_set_flag(ERR_FLAG_SERIAL_TX_DROP);
    }
  }
}

void serial_link_task(void)
{
  s_task = osThreadGetId();
  proto_decoder_init(&s_decoder);
  rx_start();

  for (;;)
  {
    uint32_t flags = osThreadFlagsWait(FLAG_ALL, osFlagsWaitAny, TASK_PERIOD_MS);
    uint32_t now = osKernelGetTickCount();

    if ((flags & osFlagsError) != 0U)
    {
      flags = 0U;   /* timeout: periodic housekeeping only */
    }

    supervisor_checkin(SUP_TASK_SERIAL);

    rx_drain(now);
    if ((flags & FLAG_RX_ERROR) != 0U)
    {
      (void)HAL_UART_AbortReceive(&huart2);
      rx_start();
    }
    proto_decoder_poll(&s_decoder, now, frame_cb, NULL);

    tx_service(now);
  }
}

/* --------------------------------------------------- HAL callbacks (ISR) --- */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  (void)size;
  if (huart->Instance == USART2)
  {
    notify_task(FLAG_RX);
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    s_tx_busy = false;
    notify_task(FLAG_TX_DONE);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    if ((huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U)
    {
      diag_set_flag(ERR_FLAG_SERIAL_OVERFLOW);
    }
    notify_task(FLAG_RX_ERROR);
  }
}
