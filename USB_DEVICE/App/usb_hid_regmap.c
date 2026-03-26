#include "usb_hid_regmap.h"
#include "regmap.h"
#include <string.h>

uint8_t HID_ProcessReport(const uint8_t *report, uint8_t *response_buf)
{
    uint8_t cmd      = report[0];
    uint8_t reg_addr = report[1];
    uint8_t data_len = report[2];

    memset(response_buf, 0, HID_REPORT_SIZE);

    switch (cmd)
    {
    case HID_CMD_READ_REQ:
    {
        /* Validate register address */
        if (reg_addr >= REG_MAP_SIZE)
        {
            response_buf[0]  = HID_CMD_ERROR;
            response_buf[1]  = reg_addr;
            response_buf[63] = HID_STATUS_INVALID_ADDR;
            break;
        }

        /* Validate length: 1..60 and must not exceed map boundary */
        if (data_len < 1 || data_len > HID_MAX_DATA_LEN ||
            (reg_addr + data_len) > REG_MAP_SIZE)
        {
            response_buf[0]  = HID_CMD_ERROR;
            response_buf[1]  = reg_addr;
            response_buf[63] = HID_STATUS_INVALID_LEN;
            break;
        }

        /* Snapshot the requested region into the response payload */
        uint8_t snap[REG_MAP_SIZE];
        uint8_t avail = RegMap_BuildSnapshot(reg_addr, snap, data_len);
        uint8_t copy_len = (data_len <= avail) ? data_len : avail;
        memcpy(&response_buf[3], snap, copy_len);

        response_buf[0]  = HID_CMD_READ_RESP;
        response_buf[1]  = reg_addr;
        response_buf[2]  = copy_len;
        response_buf[63] = HID_STATUS_OK;
        break;
    }

    case HID_CMD_WRITE_REQ:
    {
        /* Validate register address */
        if (reg_addr >= REG_MAP_SIZE)
        {
            response_buf[0]  = HID_CMD_ERROR;
            response_buf[1]  = reg_addr;
            response_buf[63] = HID_STATUS_INVALID_ADDR;
            break;
        }

        /* Validate length: 1..16 bytes (nickname register needs up to 16) */
        if (data_len < 1 || data_len > 16)
        {
            response_buf[0]  = HID_CMD_ERROR;
            response_buf[1]  = reg_addr;
            response_buf[63] = HID_STATUS_INVALID_LEN;
            break;
        }

        /* Perform the write under mutex */
        RegMap_Lock();
        uint8_t ret = RegMap_Write(reg_addr, &report[3], data_len);
        RegMap_Unlock();

        response_buf[0]  = HID_CMD_WRITE_ACK;
        response_buf[1]  = reg_addr;
        response_buf[2]  = data_len;
        response_buf[63] = (ret == 0) ? HID_STATUS_OK : HID_STATUS_WRITE_ERR;
        break;
    }

    default:
        response_buf[0]  = HID_CMD_ERROR;
        response_buf[63] = HID_STATUS_INVALID_ADDR;
        break;
    }

    return 0;
}
