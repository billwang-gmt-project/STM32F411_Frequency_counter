/**
  * @file    usbd_composite.h
  * @brief   USB Composite device class (CDC ACM + HID Custom) header
  */
#ifndef __USBD_COMPOSITE_H
#define __USBD_COMPOSITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

/* --------------- Interface numbers ---------------------------------------- */
#define CDC_CMD_ITF                     0U
#define CDC_DATA_ITF                    1U
#define HID_ITF                         2U
#define USBD_COMPOSITE_NUM_INTERFACES   3U

/* --------------- Endpoint addresses --------------------------------------- */
#define CDC_IN_EP                       0x81U
#define CDC_OUT_EP                      0x01U
#define CDC_CMD_EP                      0x82U
#define HID_IN_EP                       0x83U
#define HID_OUT_EP                      0x02U

/* --------------- Endpoint sizes ------------------------------------------- */
#define CDC_DATA_FS_MAX_PACKET_SIZE     64U
#define CDC_CMD_PACKET_SIZE             8U
#define HID_EPIN_SIZE                   64U
#define HID_EPOUT_SIZE                  64U

/* --------------- CDC class-specific defines ------------------------------- */
#define CDC_SEND_ENCAPSULATED_COMMAND   0x00U
#define CDC_GET_ENCAPSULATED_RESPONSE   0x01U
#define CDC_SET_COMM_FEATURE            0x02U
#define CDC_GET_COMM_FEATURE            0x03U
#define CDC_CLEAR_COMM_FEATURE          0x04U
#define CDC_SET_LINE_CODING             0x20U
#define CDC_GET_LINE_CODING             0x21U
#define CDC_SET_CONTROL_LINE_STATE      0x22U
#define CDC_SEND_BREAK                  0x23U

/* --------------- HID class-specific defines ------------------------------- */
#define HID_REQ_SET_PROTOCOL            0x0BU
#define HID_REQ_GET_PROTOCOL            0x03U
#define HID_REQ_SET_IDLE                0x0AU
#define HID_REQ_GET_IDLE                0x02U
#define HID_REQ_SET_REPORT              0x09U
#define HID_REQ_GET_REPORT              0x01U
#define HID_DESCRIPTOR_TYPE             0x21U
#define HID_REPORT_DESC_TYPE            0x22U

/* --------------- Polling intervals ---------------------------------------- */
#define CDC_FS_BINTERVAL                16U
#define HID_FS_BINTERVAL                1U

/* --------------- CDC Line Coding ------------------------------------------ */
typedef struct {
  uint32_t dwDTERate;
  uint8_t  bCharFormat;
  uint8_t  bParityType;
  uint8_t  bDataBits;
} __attribute__((packed)) USBD_CDC_LineCodingTypeDef;

/* --------------- Callback types ------------------------------------------- */
typedef int8_t (*USBD_CDC_ReceiveCallback_t)(uint8_t *buf, uint32_t *len);
typedef int8_t (*USBD_CDC_InitCallback_t)(void);
typedef int8_t (*USBD_CDC_DeInitCallback_t)(void);
typedef int8_t (*USBD_CDC_ControlCallback_t)(uint8_t cmd, uint8_t *pbuf, uint16_t length);
typedef int8_t (*USBD_HID_ReceiveCallback_t)(uint8_t *buf, uint16_t len);

/* --------------- CDC Interface Operations --------------------------------- */
typedef struct {
  USBD_CDC_InitCallback_t     Init;
  USBD_CDC_DeInitCallback_t   DeInit;
  USBD_CDC_ControlCallback_t  Control;
  USBD_CDC_ReceiveCallback_t  Receive;
} USBD_CDC_ItfTypeDef;

/* --------------- Composite class data struct ------------------------------ */
typedef struct {
  uint8_t                      cdc_cmd_buf[CDC_CMD_PACKET_SIZE];
  uint8_t                      cdc_rx_buf[CDC_DATA_FS_MAX_PACKET_SIZE];
  uint8_t                      cdc_tx_buf[CDC_DATA_FS_MAX_PACKET_SIZE];
  USBD_CDC_LineCodingTypeDef   cdc_line_coding;
  uint8_t                      cdc_cmd_opcode;
  uint8_t                      cdc_cmd_len;
  volatile uint8_t             cdc_tx_busy;

  uint8_t                      hid_rx_buf[HID_EPOUT_SIZE];
  volatile uint8_t             hid_tx_busy;
  uint8_t                      hid_protocol;
  uint8_t                      hid_idle_state;
  uint8_t                      hid_alt_setting;

  USBD_CDC_ItfTypeDef         *cdc_fops;
  USBD_HID_ReceiveCallback_t   hid_rx_cb;
} USBD_COMPOSITE_HandleTypeDef;

/* --------------- Exported class instance ---------------------------------- */
extern USBD_ClassTypeDef USBD_COMPOSITE;

/* --------------- Public API ----------------------------------------------- */
uint8_t USBD_COMPOSITE_RegisterCDCInterface(USBD_HandleTypeDef *pdev,
                                             USBD_CDC_ItfTypeDef *fops);
uint8_t USBD_COMPOSITE_RegisterHIDCallback(USBD_HandleTypeDef *pdev,
                                            USBD_HID_ReceiveCallback_t cb);
uint8_t USBD_CDC_Transmit(USBD_HandleTypeDef *pdev,
                           uint8_t *buf, uint16_t len);
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev,
                              uint8_t *report, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_COMPOSITE_H */
