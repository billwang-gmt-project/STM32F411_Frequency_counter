#ifndef CDC_FIFO_H
#define CDC_FIFO_H

#include <stdint.h>

void     CDC_FIFO_Init(void);

/* RX FIFO — ISR pushes, UsbTask pops */
void     CDC_RxPush(const uint8_t *data, uint16_t len);
uint16_t CDC_RxAvailable(void);
int16_t  CDC_RxPopByte(void);  /* returns -1 if empty */

/* TX FIFO — task pushes, UsbTask drains */
void     CDC_TxPush(const uint8_t *data, uint16_t len);
void     CDC_TxPushStr(const char *str);
uint16_t CDC_TxAvailable(void);
uint16_t CDC_TxPop(uint8_t *buf, uint16_t max);

/* Semaphore: ISR gives when RX data arrives */
void     CDC_RxNotifyFromISR(void);

/* Check if TX busy flag is clear (peek into composite handle) */
uint8_t  CDC_TxBusy(void);

/* Wait for RX data with timeout (ms). Returns pdTRUE if signaled. */
uint32_t CDC_RxWait(uint32_t timeout_ms);

#endif
