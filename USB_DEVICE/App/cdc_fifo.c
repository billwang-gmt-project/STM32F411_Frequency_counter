#include "cdc_fifo.h"
#include "usbd_composite.h"
#include "usb_device.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* ---- Ring buffer sizes ---- */
#define RX_FIFO_SIZE  512U
#define TX_FIFO_SIZE  2048U

/* ---- RX ring buffer (ISR writes, task reads) ---- */
static uint8_t  rx_buf[RX_FIFO_SIZE];
static volatile uint16_t rx_head;  /* written by ISR */
static volatile uint16_t rx_tail;  /* read by task */

/* ---- TX ring buffer (task writes, task reads for USB send) ---- */
static uint8_t  tx_buf[TX_FIFO_SIZE];
static volatile uint16_t tx_head;  /* written by task (push) */
static volatile uint16_t tx_tail;  /* read by task (pop/send) */

/* ---- Semaphore to wake UsbTask ---- */
static SemaphoreHandle_t rx_sem;

/* ---- Init ---- */
void CDC_FIFO_Init(void)
{
  rx_head = rx_tail = 0;
  tx_head = tx_tail = 0;
  rx_sem = xSemaphoreCreateBinary();
}

/* ---- RX FIFO ---- */

void CDC_RxPush(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
  {
    uint16_t next = (rx_head + 1U) % RX_FIFO_SIZE;
    if (next == rx_tail) break;  /* overflow — drop */
    rx_buf[rx_head] = data[i];
    rx_head = next;
  }
}

void CDC_RxNotifyFromISR(void)
{
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(rx_sem, &woken);
  portYIELD_FROM_ISR(woken);
}

uint16_t CDC_RxAvailable(void)
{
  uint16_t h = rx_head;
  uint16_t t = rx_tail;
  return (h >= t) ? (h - t) : (RX_FIFO_SIZE - t + h);
}

int16_t CDC_RxPopByte(void)
{
  if (rx_head == rx_tail) return -1;
  uint8_t c = rx_buf[rx_tail];
  rx_tail = (rx_tail + 1U) % RX_FIFO_SIZE;
  return (int16_t)c;
}

/* ---- TX FIFO ---- */

void CDC_TxPush(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
  {
    uint16_t next = (tx_head + 1U) % TX_FIFO_SIZE;
    if (next == tx_tail) break;  /* overflow — drop */
    tx_buf[tx_head] = data[i];
    tx_head = next;
  }
}

void CDC_TxPushStr(const char *str)
{
  while (*str)
  {
    uint16_t next = (tx_head + 1U) % TX_FIFO_SIZE;
    if (next == tx_tail) break;
    tx_buf[tx_head] = (uint8_t)*str++;
    tx_head = next;
  }
}

uint16_t CDC_TxAvailable(void)
{
  uint16_t h = tx_head;
  uint16_t t = tx_tail;
  return (h >= t) ? (h - t) : (TX_FIFO_SIZE - t + h);
}

uint16_t CDC_TxPop(uint8_t *buf, uint16_t max)
{
  uint16_t count = 0;
  while (count < max && tx_head != tx_tail)
  {
    buf[count++] = tx_buf[tx_tail];
    tx_tail = (tx_tail + 1U) % TX_FIFO_SIZE;
  }
  return count;
}

uint8_t CDC_TxBusy(void)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)hUsbDeviceFS.pClassData;
  if (hcomp == NULL) return 1U;
  return hcomp->cdc_tx_busy;
}

uint32_t CDC_RxWait(uint32_t timeout_ms)
{
  return (uint32_t)xSemaphoreTake(rx_sem, pdMS_TO_TICKS(timeout_ms));
}
