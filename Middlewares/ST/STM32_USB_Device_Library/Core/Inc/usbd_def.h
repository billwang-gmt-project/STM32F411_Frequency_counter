/**
  * @file    usbd_def.h
  * @brief   USB Device definitions — types, constants, and low-level prototypes
  */
#ifndef __USBD_DEF_H
#define __USBD_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_conf.h"

#ifndef NULL
#define NULL  ((void *)0)
#endif

/* ---- USB Descriptor types ---- */
#define USB_DESC_TYPE_DEVICE                    0x01U
#define USB_DESC_TYPE_CONFIGURATION             0x02U
#define USB_DESC_TYPE_STRING                    0x03U
#define USB_DESC_TYPE_INTERFACE                 0x04U
#define USB_DESC_TYPE_ENDPOINT                  0x05U
#define USB_DESC_TYPE_DEVICE_QUALIFIER          0x06U
#define USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION 0x07U
#define USB_DESC_TYPE_IAD                       0x0BU

/* ---- USB Request types ---- */
#define USB_REQ_TYPE_MASK       0x60U
#define USB_REQ_TYPE_STANDARD   0x00U
#define USB_REQ_TYPE_CLASS      0x20U
#define USB_REQ_TYPE_VENDOR     0x40U
#define USB_REQ_RECIPIENT_MASK  0x1FU
#define USB_REQ_RECIPIENT_DEVICE    0x00U
#define USB_REQ_RECIPIENT_INTERFACE 0x01U
#define USB_REQ_RECIPIENT_ENDPOINT  0x02U

/* ---- Standard Request codes ---- */
#define USB_REQ_GET_STATUS        0x00U
#define USB_REQ_CLEAR_FEATURE     0x01U
#define USB_REQ_SET_FEATURE       0x03U
#define USB_REQ_SET_ADDRESS       0x05U
#define USB_REQ_GET_DESCRIPTOR    0x06U
#define USB_REQ_SET_DESCRIPTOR    0x07U
#define USB_REQ_GET_CONFIGURATION 0x08U
#define USB_REQ_SET_CONFIGURATION 0x09U
#define USB_REQ_GET_INTERFACE     0x0AU
#define USB_REQ_SET_INTERFACE     0x0BU
#define USB_REQ_SYNCH_FRAME       0x0CU

/* ---- Feature selectors ---- */
#define USB_FEATURE_EP_HALT       0x00U
#define USB_FEATURE_REMOTE_WAKEUP 0x01U
#define USB_FEATURE_TEST_MODE     0x02U

/* ---- Endpoint types ---- */
#define USBD_EP_TYPE_CTRL   0x00U
#define USBD_EP_TYPE_ISOC   0x01U
#define USBD_EP_TYPE_BULK   0x02U
#define USBD_EP_TYPE_INTR   0x03U

/* ---- Descriptor lengths ---- */
#define USB_LEN_DEV_QUALIFIER_DESC  0x0AU
#define USB_LEN_DEV_DESC            0x12U
#define USB_LEN_CFG_DESC            0x09U
#define USB_LEN_IF_DESC             0x09U
#define USB_LEN_EP_DESC             0x07U
#define USB_LEN_OTG_DESC            0x03U
#define USB_LEN_LANGID_STR_DESC     0x04U

/* ---- Speed / Device identifiers ---- */
#define DEVICE_FS       0U
#define DEVICE_HS       1U
#define USBD_SPEED_HIGH 0U
#define USBD_SPEED_FULL 1U
#define USBD_SPEED_LOW  2U

/* ---- Device states ---- */
#define USBD_STATE_DEFAULT    1U
#define USBD_STATE_ADDRESSED  2U
#define USBD_STATE_CONFIGURED 3U
#define USBD_STATE_SUSPENDED  4U

/* ---- EP0 states ---- */
#define USBD_EP0_IDLE       0U
#define USBD_EP0_SETUP      1U
#define USBD_EP0_DATA_IN    2U
#define USBD_EP0_DATA_OUT   3U
#define USBD_EP0_STATUS_IN  4U
#define USBD_EP0_STATUS_OUT 5U
#define USBD_EP0_STALL      6U

/* ---- Utility macros ---- */
#define LOBYTE(x)   ((uint8_t)((x) & 0x00FFU))
#define HIBYTE(x)   ((uint8_t)(((x) & 0xFF00U) >> 8U))
#define SWAPBYTE(addr) (((uint16_t)(*(addr))) | \
                        ((uint16_t)(*((addr) + 1U)) << 8U))
#ifndef MIN
#define MIN(a, b)   (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))
#endif

/* ---- Status codes ---- */
typedef enum {
  USBD_OK   = 0U,
  USBD_BUSY = 1U,
  USBD_EMEM = 2U,
  USBD_FAIL = 3U,
} USBD_StatusTypeDef;

typedef uint8_t USBD_SpeedTypeDef;

/* ---- Setup request ---- */
typedef struct {
  uint8_t  bmRequest;
  uint8_t  bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} USBD_SetupReqTypedef;

/* ---- Endpoint state ---- */
typedef struct {
  uint32_t status;
  uint32_t total_length;
  uint32_t rem_length;
  uint32_t maxpacket;
  uint16_t is_used;
  uint16_t bInterval;
} USBD_EndpointTypeDef;

/* ---- Forward declaration ---- */
struct _USBD_HandleTypeDef;

/* ---- Class driver interface ---- */
typedef struct {
  uint8_t (*Init)(struct _USBD_HandleTypeDef *pdev, uint8_t cfgidx);
  uint8_t (*DeInit)(struct _USBD_HandleTypeDef *pdev, uint8_t cfgidx);
  uint8_t (*Setup)(struct _USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
  uint8_t (*EP0_TxSent)(struct _USBD_HandleTypeDef *pdev);
  uint8_t (*EP0_RxReady)(struct _USBD_HandleTypeDef *pdev);
  uint8_t (*DataIn)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
  uint8_t (*DataOut)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
  uint8_t (*SOF)(struct _USBD_HandleTypeDef *pdev);
  uint8_t (*IsoINIncomplete)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
  uint8_t (*IsoOUTIncomplete)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
  uint8_t *(*GetFSConfigDescriptor)(uint16_t *length);
  uint8_t *(*GetHSConfigDescriptor)(uint16_t *length);
  uint8_t *(*GetOtherSpeedConfigDescriptor)(uint16_t *length);
  uint8_t *(*GetDeviceQualifierDescriptor)(uint16_t *length);
} USBD_ClassTypeDef;

/* ---- Descriptor callbacks ---- */
typedef struct {
  uint8_t *(*GetDeviceDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
  uint8_t *(*GetLangIDStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
  uint8_t *(*GetManufacturerStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
  uint8_t *(*GetProductStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
  uint8_t *(*GetSerialStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
  uint8_t *(*GetConfigurationStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
  uint8_t *(*GetInterfaceStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
} USBD_DescriptorsTypeDef;

/* ---- Device handle ---- */
typedef struct _USBD_HandleTypeDef {
  uint8_t                   id;
  uint32_t                  dev_config;
  uint32_t                  dev_default_config;
  uint32_t                  dev_config_status;
  USBD_SpeedTypeDef         dev_speed;
  USBD_EndpointTypeDef      ep_in[16];
  USBD_EndpointTypeDef      ep_out[16];
  __ALIGN_BEGIN uint32_t    setup_buf[12] __ALIGN_END;
  USBD_SetupReqTypedef      request;
  USBD_DescriptorsTypeDef   *pDesc;
  USBD_ClassTypeDef         *pClass;
  void                      *pClassData;
  void                      *pUserData;
  void                      *pData;       /* PCD handle */
  uint8_t                   dev_state;
  uint8_t                   dev_old_state;
  uint8_t                   dev_address;
  uint8_t                   dev_connection_status;
  uint8_t                   dev_test_mode;
  uint32_t                  dev_remote_wakeup;
  uint8_t                   ep0_state;
  uint32_t                  ep0_data_len;
  uint8_t                   dev_config_descriptor[USB_LEN_CFG_DESC];
} USBD_HandleTypeDef;

/* ---- Low-level function prototypes (implemented in usbd_conf.c) ---- */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                   uint8_t ep_type, uint16_t ep_mps);
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
uint8_t            USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr);
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                     uint8_t *pbuf, uint32_t size);
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                           uint8_t *pbuf, uint32_t size);
uint32_t           USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
void               USBD_LL_Delay(uint32_t Delay);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_DEF_H */
