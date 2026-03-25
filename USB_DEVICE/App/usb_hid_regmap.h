#ifndef USB_HID_REGMAP_H
#define USB_HID_REGMAP_H

#include <stdint.h>

/* HID command/response codes */
#define HID_CMD_READ_REQ     0x01
#define HID_CMD_READ_RESP    0x02
#define HID_CMD_WRITE_REQ    0x03
#define HID_CMD_WRITE_ACK    0x04
#define HID_CMD_ERROR        0xFF

/* HID status codes */
#define HID_STATUS_OK            0x00
#define HID_STATUS_INVALID_ADDR  0x01
#define HID_STATUS_INVALID_LEN   0x02
#define HID_STATUS_WRITE_ERR     0x03

#define HID_REPORT_SIZE  64
#define HID_MAX_DATA_LEN 60  /* 64 - 4 header bytes */

/* Process a received HID OUT report (64 bytes).
 * Fills response_buf (64 bytes) with the IN report to send back.
 * Returns 0 on success. */
uint8_t HID_ProcessReport(const uint8_t *report, uint8_t *response_buf);

#endif /* USB_HID_REGMAP_H */
