#ifndef USB_CDC_CMD_H
#define USB_CDC_CMD_H

#include <stdint.h>

/* Parse a single complete command line (NUL-terminated, no \r\n).
 * Writes response into TX FIFO via CDC_TxPush(). */
void CDC_ParseLine(const char *line);

/* Legacy API — still available if needed */
uint16_t CDC_ProcessRxData(const uint8_t *data, uint16_t len,
                           uint8_t *response_buf, uint16_t response_max);

#endif
