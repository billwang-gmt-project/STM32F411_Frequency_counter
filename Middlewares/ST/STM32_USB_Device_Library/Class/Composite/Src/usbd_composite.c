/**
  * @file    usbd_composite.c
  * @brief   USB Composite device class: CDC ACM + HID Custom
  *
  * Configuration descriptor layout:
  *   Config (9) + IAD (8) + CDC Comm Itf (9+5+5+4+5+7=35) +
  *   CDC Data Itf (9+7+7=23) + HID Itf (9+9+7+7=32) = 107 bytes
  */
#include "usbd_composite.h"
#include "usbd_ctlreq.h"
#include <string.h>

/* ---------- Private macros ------------------------------------------------ */
#define COMPOSITE_CONFIG_DESC_SIZE   107U
#define HID_REPORT_DESC_SIZE         34U

/* ---------- Static function prototypes ------------------------------------ */
static uint8_t USBD_COMPOSITE_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_COMPOSITE_Setup(USBD_HandleTypeDef *pdev,
                                     USBD_SetupReqTypedef *req);
static uint8_t USBD_COMPOSITE_EP0_TxSent(USBD_HandleTypeDef *pdev);
static uint8_t USBD_COMPOSITE_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_COMPOSITE_SOF(USBD_HandleTypeDef *pdev);
static uint8_t *USBD_COMPOSITE_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length);

/* ---------- Static class data (single instance) --------------------------- */
static USBD_COMPOSITE_HandleTypeDef hComposite;

/* ---------- HID report descriptor: vendor-defined, 64B IN + 64B OUT ------- */
static const uint8_t HID_ReportDesc[HID_REPORT_DESC_SIZE] = {
  0x06, 0x00, 0xFF,  /* Usage Page (Vendor Defined 0xFF00) */
  0x09, 0x01,        /* Usage (Vendor Usage 1) */
  0xA1, 0x01,        /* Collection (Application) */

  /* Input report: 64 bytes */
  0x09, 0x01,        /*   Usage (Vendor Usage 1) */
  0x15, 0x00,        /*   Logical Minimum (0) */
  0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
  0x75, 0x08,        /*   Report Size (8) */
  0x95, 0x40,        /*   Report Count (64) */
  0x81, 0x02,        /*   Input (Data, Var, Abs) */

  /* Output report: 64 bytes */
  0x09, 0x01,        /*   Usage (Vendor Usage 1) */
  0x15, 0x00,        /*   Logical Minimum (0) */
  0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
  0x75, 0x08,        /*   Report Size (8) */
  0x95, 0x40,        /*   Report Count (64) */
  0x91, 0x02,        /*   Output (Data, Var, Abs) */

  0xC0               /* End Collection */
};

/* ---------- Configuration descriptor -------------------------------------- */
static uint8_t USBD_COMPOSITE_CfgDesc[COMPOSITE_CONFIG_DESC_SIZE] = {
  /* --- Configuration Descriptor (9 bytes) --- */
  0x09,                              /* bLength */
  USB_DESC_TYPE_CONFIGURATION,       /* bDescriptorType */
  LOBYTE(COMPOSITE_CONFIG_DESC_SIZE),/* wTotalLength lo */
  HIBYTE(COMPOSITE_CONFIG_DESC_SIZE),/* wTotalLength hi */
  0x03,                              /* bNumInterfaces (CDC=2 + HID=1) */
  0x01,                              /* bConfigurationValue */
  0x00,                              /* iConfiguration */
  0x80,                              /* bmAttributes: bus-powered */
  0xFA,                              /* bMaxPower: 500mA */

  /* ================================================================== */
  /*                       CDC ACM (2 interfaces)                        */
  /* ================================================================== */

  /* --- IAD: Interface Association Descriptor (8 bytes) --- */
  0x08,                              /* bLength */
  USB_DESC_TYPE_IAD,                 /* bDescriptorType = 0x0B */
  CDC_CMD_ITF,                       /* bFirstInterface = 0 */
  0x02,                              /* bInterfaceCount = 2 */
  0x02,                              /* bFunctionClass: CDC */
  0x02,                              /* bFunctionSubClass: ACM */
  0x01,                              /* bFunctionProtocol: AT commands */
  0x00,                              /* iFunction */

  /* --- Interface 0: CDC Communication Interface (9 bytes) --- */
  0x09,                              /* bLength */
  USB_DESC_TYPE_INTERFACE,           /* bDescriptorType */
  CDC_CMD_ITF,                       /* bInterfaceNumber = 0 */
  0x00,                              /* bAlternateSetting */
  0x01,                              /* bNumEndpoints = 1 (CMD) */
  0x02,                              /* bInterfaceClass: CDC */
  0x02,                              /* bInterfaceSubClass: ACM */
  0x01,                              /* bInterfaceProtocol: AT commands */
  0x00,                              /* iInterface */

  /* --- CDC Header Functional Descriptor (5 bytes) --- */
  0x05,                              /* bLength */
  0x24,                              /* bDescriptorType: CS_INTERFACE */
  0x00,                              /* bDescriptorSubtype: Header */
  0x10, 0x01,                        /* bcdCDC: 1.10 */

  /* --- CDC Call Management Functional Descriptor (5 bytes) --- */
  0x05,                              /* bLength */
  0x24,                              /* bDescriptorType: CS_INTERFACE */
  0x01,                              /* bDescriptorSubtype: Call Management */
  0x00,                              /* bmCapabilities */
  CDC_DATA_ITF,                      /* bDataInterface = 1 */

  /* --- CDC ACM Functional Descriptor (4 bytes) --- */
  0x04,                              /* bLength */
  0x24,                              /* bDescriptorType: CS_INTERFACE */
  0x02,                              /* bDescriptorSubtype: ACM */
  0x02,                              /* bmCapabilities: line coding + serial state */

  /* --- CDC Union Functional Descriptor (5 bytes) --- */
  0x05,                              /* bLength */
  0x24,                              /* bDescriptorType: CS_INTERFACE */
  0x06,                              /* bDescriptorSubtype: Union */
  CDC_CMD_ITF,                       /* bMasterInterface = 0 */
  CDC_DATA_ITF,                      /* bSlaveInterface0 = 1 */

  /* --- Endpoint: CDC CMD IN (7 bytes) --- */
  0x07,                              /* bLength */
  USB_DESC_TYPE_ENDPOINT,            /* bDescriptorType */
  CDC_CMD_EP,                        /* bEndpointAddress = 0x82 IN */
  0x03,                              /* bmAttributes: Interrupt */
  LOBYTE(CDC_CMD_PACKET_SIZE),       /* wMaxPacketSize lo */
  HIBYTE(CDC_CMD_PACKET_SIZE),       /* wMaxPacketSize hi */
  CDC_FS_BINTERVAL,                  /* bInterval */

  /* --- Interface 1: CDC Data Interface (9 bytes) --- */
  0x09,                              /* bLength */
  USB_DESC_TYPE_INTERFACE,           /* bDescriptorType */
  CDC_DATA_ITF,                      /* bInterfaceNumber = 1 */
  0x00,                              /* bAlternateSetting */
  0x02,                              /* bNumEndpoints = 2 (IN + OUT) */
  0x0A,                              /* bInterfaceClass: CDC Data */
  0x00,                              /* bInterfaceSubClass */
  0x00,                              /* bInterfaceProtocol */
  0x00,                              /* iInterface */

  /* --- Endpoint: CDC Data OUT (7 bytes) --- */
  0x07,                              /* bLength */
  USB_DESC_TYPE_ENDPOINT,            /* bDescriptorType */
  CDC_OUT_EP,                        /* bEndpointAddress = 0x01 OUT */
  0x02,                              /* bmAttributes: Bulk */
  LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  0x00,                              /* bInterval (ignored for bulk) */

  /* --- Endpoint: CDC Data IN (7 bytes) --- */
  0x07,                              /* bLength */
  USB_DESC_TYPE_ENDPOINT,            /* bDescriptorType */
  CDC_IN_EP,                         /* bEndpointAddress = 0x81 IN */
  0x02,                              /* bmAttributes: Bulk */
  LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  0x00,                              /* bInterval */

  /* ================================================================== */
  /*                       HID Custom (1 interface)                      */
  /* ================================================================== */

  /* --- Interface 2: HID Custom (9 bytes) --- */
  0x09,                              /* bLength */
  USB_DESC_TYPE_INTERFACE,           /* bDescriptorType */
  HID_ITF,                           /* bInterfaceNumber = 2 */
  0x00,                              /* bAlternateSetting */
  0x02,                              /* bNumEndpoints = 2 (IN + OUT) */
  0x03,                              /* bInterfaceClass: HID */
  0x00,                              /* bInterfaceSubClass: No boot */
  0x00,                              /* bInterfaceProtocol: None */
  0x00,                              /* iInterface */

  /* --- HID Descriptor (9 bytes) --- */
  0x09,                              /* bLength */
  HID_DESCRIPTOR_TYPE,               /* bDescriptorType = 0x21 */
  0x11, 0x01,                        /* bcdHID: 1.11 */
  0x00,                              /* bCountryCode */
  0x01,                              /* bNumDescriptors */
  HID_REPORT_DESC_TYPE,              /* bDescriptorType = 0x22 */
  LOBYTE(HID_REPORT_DESC_SIZE),      /* wDescriptorLength lo */
  HIBYTE(HID_REPORT_DESC_SIZE),      /* wDescriptorLength hi */

  /* --- Endpoint: HID IN (7 bytes) --- */
  0x07,                              /* bLength */
  USB_DESC_TYPE_ENDPOINT,            /* bDescriptorType */
  HID_IN_EP,                         /* bEndpointAddress = 0x83 IN */
  0x03,                              /* bmAttributes: Interrupt */
  LOBYTE(HID_EPIN_SIZE),             /* wMaxPacketSize lo */
  HIBYTE(HID_EPIN_SIZE),             /* wMaxPacketSize hi */
  HID_FS_BINTERVAL,                  /* bInterval = 1ms */

  /* --- Endpoint: HID OUT (7 bytes) --- */
  0x07,                              /* bLength */
  USB_DESC_TYPE_ENDPOINT,            /* bDescriptorType */
  HID_OUT_EP,                        /* bEndpointAddress = 0x02 OUT */
  0x03,                              /* bmAttributes: Interrupt */
  LOBYTE(HID_EPOUT_SIZE),            /* wMaxPacketSize lo */
  HIBYTE(HID_EPOUT_SIZE),            /* wMaxPacketSize hi */
  HID_FS_BINTERVAL,                  /* bInterval = 1ms */
};

/* ---------- Device Qualifier Descriptor ----------------------------------- */
static uint8_t USBD_COMPOSITE_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] = {
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02,   /* bcdUSB: 2.00 */
  0xEF,          /* bDeviceClass: Misc (IAD) */
  0x02,          /* bDeviceSubClass */
  0x01,          /* bDeviceProtocol */
  USB_MAX_EP0_SIZE,
  0x01,          /* bNumConfigurations */
  0x00,          /* bReserved */
};

/* ---------- Class driver instance ----------------------------------------- */
USBD_ClassTypeDef USBD_COMPOSITE = {
  USBD_COMPOSITE_Init,
  USBD_COMPOSITE_DeInit,
  USBD_COMPOSITE_Setup,
  USBD_COMPOSITE_EP0_TxSent,
  USBD_COMPOSITE_EP0_RxReady,
  USBD_COMPOSITE_DataIn,
  USBD_COMPOSITE_DataOut,
  USBD_COMPOSITE_SOF,
  NULL, /* IsoINIncomplete */
  NULL, /* IsoOUTIncomplete */
  USBD_COMPOSITE_GetFSCfgDesc,
  USBD_COMPOSITE_GetFSCfgDesc,     /* HS = FS (full-speed only) */
  USBD_COMPOSITE_GetFSCfgDesc,     /* Other = FS */
  USBD_COMPOSITE_GetDeviceQualifierDesc,
};

/* ========================================================================== */
/*                          CLASS CALLBACKS                                    */
/* ========================================================================== */

static uint8_t USBD_COMPOSITE_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;
  USBD_COMPOSITE_HandleTypeDef *hcomp = &hComposite;

  /* Save registered callbacks (set before enumeration, must survive re-init) */
  USBD_CDC_ItfTypeDef *saved_cdc_fops = hcomp->cdc_fops;
  USBD_HID_ReceiveCallback_t saved_hid_cb = hcomp->hid_rx_cb;

  /* Zero-init state */
  memset(hcomp, 0, sizeof(*hcomp));

  /* Restore callbacks */
  hcomp->cdc_fops  = saved_cdc_fops;
  hcomp->hid_rx_cb = saved_hid_cb;

  /* Default line coding */
  hcomp->cdc_line_coding.dwDTERate   = 115200U;
  hcomp->cdc_line_coding.bCharFormat = 0U;
  hcomp->cdc_line_coding.bParityType = 0U;
  hcomp->cdc_line_coding.bDataBits   = 8U;

  /* Store handle */
  pdev->pClassData = (void *)hcomp;

  /* --- Open CDC endpoints --- */
  USBD_LL_OpenEP(pdev, CDC_IN_EP,  USBD_EP_TYPE_BULK, CDC_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_in[CDC_IN_EP & 0x0FU].is_used = 1U;

  USBD_LL_OpenEP(pdev, CDC_OUT_EP, USBD_EP_TYPE_BULK, CDC_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_out[CDC_OUT_EP & 0x0FU].is_used = 1U;

  USBD_LL_OpenEP(pdev, CDC_CMD_EP, USBD_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
  pdev->ep_in[CDC_CMD_EP & 0x0FU].is_used = 1U;

  /* --- Open HID endpoints --- */
  USBD_LL_OpenEP(pdev, HID_IN_EP,  USBD_EP_TYPE_INTR, HID_EPIN_SIZE);
  pdev->ep_in[HID_IN_EP & 0x0FU].is_used = 1U;

  USBD_LL_OpenEP(pdev, HID_OUT_EP, USBD_EP_TYPE_INTR, HID_EPOUT_SIZE);
  pdev->ep_out[HID_OUT_EP & 0x0FU].is_used = 1U;

  /* --- Arm CDC OUT receive --- */
  USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcomp->cdc_rx_buf,
                          CDC_DATA_FS_MAX_PACKET_SIZE);

  /* --- Arm HID OUT receive --- */
  USBD_LL_PrepareReceive(pdev, HID_OUT_EP, hcomp->hid_rx_buf, HID_EPOUT_SIZE);

  /* --- Notify CDC application --- */
  if (hcomp->cdc_fops != NULL && hcomp->cdc_fops->Init != NULL)
  {
    hcomp->cdc_fops->Init();
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  /* Close all endpoints */
  USBD_LL_CloseEP(pdev, CDC_IN_EP);
  pdev->ep_in[CDC_IN_EP & 0x0FU].is_used = 0U;
  USBD_LL_CloseEP(pdev, CDC_OUT_EP);
  pdev->ep_out[CDC_OUT_EP & 0x0FU].is_used = 0U;
  USBD_LL_CloseEP(pdev, CDC_CMD_EP);
  pdev->ep_in[CDC_CMD_EP & 0x0FU].is_used = 0U;
  USBD_LL_CloseEP(pdev, HID_IN_EP);
  pdev->ep_in[HID_IN_EP & 0x0FU].is_used = 0U;
  USBD_LL_CloseEP(pdev, HID_OUT_EP);
  pdev->ep_out[HID_OUT_EP & 0x0FU].is_used = 0U;

  /* Notify CDC application */
  if (hcomp != NULL && hcomp->cdc_fops != NULL && hcomp->cdc_fops->DeInit != NULL)
  {
    hcomp->cdc_fops->DeInit();
  }

  pdev->pClassData = NULL;
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_COMPOSITE_Setup(USBD_HandleTypeDef *pdev,
                                     USBD_SetupReqTypedef *req)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  if (hcomp == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  uint8_t itf = LOBYTE(req->wIndex);
  uint16_t len = 0U;
  uint8_t ret = (uint8_t)USBD_OK;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS:
    /* --- CDC class requests (interfaces 0 or 1) --- */
    if (itf == CDC_CMD_ITF || itf == CDC_DATA_ITF)
    {
      if (req->wLength != 0U)
      {
        if ((req->bmRequest & 0x80U) != 0U)
        {
          /* Device-to-host: return line coding etc. */
          if (hcomp->cdc_fops != NULL && hcomp->cdc_fops->Control != NULL)
          {
            hcomp->cdc_fops->Control(req->bRequest,
                                      (uint8_t *)&hcomp->cdc_line_coding,
                                      req->wLength);
          }
          USBD_CtlSendData(pdev, (uint8_t *)&hcomp->cdc_line_coding,
                            MIN(req->wLength, 7U));
        }
        else
        {
          /* Host-to-device: prepare to receive data */
          hcomp->cdc_cmd_opcode = req->bRequest;
          hcomp->cdc_cmd_len    = (uint8_t)MIN(req->wLength, CDC_CMD_PACKET_SIZE);
          USBD_CtlPrepareRx(pdev, hcomp->cdc_cmd_buf, hcomp->cdc_cmd_len);
        }
      }
      else
      {
        /* No data phase */
        if (hcomp->cdc_fops != NULL && hcomp->cdc_fops->Control != NULL)
        {
          hcomp->cdc_fops->Control(req->bRequest, NULL, 0U);
        }
      }
    }
    /* --- HID class requests (interface 2) --- */
    else if (itf == HID_ITF)
    {
      switch (req->bRequest)
      {
      case HID_REQ_SET_PROTOCOL:
        hcomp->hid_protocol = (uint8_t)(req->wValue);
        break;
      case HID_REQ_GET_PROTOCOL:
        USBD_CtlSendData(pdev, &hcomp->hid_protocol, 1U);
        break;
      case HID_REQ_SET_IDLE:
        hcomp->hid_idle_state = (uint8_t)(req->wValue >> 8);
        break;
      case HID_REQ_GET_IDLE:
        USBD_CtlSendData(pdev, &hcomp->hid_idle_state, 1U);
        break;
      case HID_REQ_SET_REPORT:
        /* Host sends a report via EP0 — prepare to receive */
        USBD_CtlPrepareRx(pdev, hcomp->hid_rx_buf,
                            MIN(req->wLength, HID_EPOUT_SIZE));
        break;
      default:
        USBD_CtlError(pdev, req);
        ret = (uint8_t)USBD_FAIL;
        break;
      }
    }
    break;

  case USB_REQ_TYPE_STANDARD:
    /* --- GET_DESCRIPTOR for HID --- */
    if (itf == HID_ITF)
    {
      switch (req->wValue >> 8)
      {
      case HID_REPORT_DESC_TYPE:
        len = MIN(HID_REPORT_DESC_SIZE, req->wLength);
        USBD_CtlSendData(pdev, (uint8_t *)HID_ReportDesc, len);
        break;

      case HID_DESCRIPTOR_TYPE:
        /* Return the 9-byte HID descriptor from the config descriptor.
           It starts at offset 84 (after config+IAD+CDC interfaces). */
        len = MIN(9U, req->wLength);
        USBD_CtlSendData(pdev, &USBD_COMPOSITE_CfgDesc[84], len);
        break;

      default:
        USBD_CtlError(pdev, req);
        ret = (uint8_t)USBD_FAIL;
        break;
      }
    }
    else
    {
      /* Standard interface requests for CDC handled by core */
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    ret = (uint8_t)USBD_FAIL;
    break;
  }

  return ret;
}

static uint8_t USBD_COMPOSITE_EP0_TxSent(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_COMPOSITE_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  if (hcomp == NULL) return (uint8_t)USBD_FAIL;

  /* CDC class data received on EP0 (e.g., SET_LINE_CODING) */
  if (hcomp->cdc_cmd_opcode != 0xFFU)
  {
    if (hcomp->cdc_fops != NULL && hcomp->cdc_fops->Control != NULL)
    {
      hcomp->cdc_fops->Control(hcomp->cdc_cmd_opcode,
                                hcomp->cdc_cmd_buf, hcomp->cdc_cmd_len);
    }

    /* Apply line coding if that was the command */
    if (hcomp->cdc_cmd_opcode == CDC_SET_LINE_CODING &&
        hcomp->cdc_cmd_len >= 7U)
    {
      memcpy(&hcomp->cdc_line_coding, hcomp->cdc_cmd_buf, 7U);
    }

    hcomp->cdc_cmd_opcode = 0xFFU;
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  if (hcomp == NULL) return (uint8_t)USBD_FAIL;

  if (epnum == (CDC_IN_EP & 0x0FU))
  {
    /* CDC bulk IN completed — mark TX as free */
    hcomp->cdc_tx_busy = 0U;
  }
  else if (epnum == (HID_IN_EP & 0x0FU))
  {
    /* HID IN report sent — mark TX as free */
    hcomp->hid_tx_busy = 0U;
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  if (hcomp == NULL) return (uint8_t)USBD_FAIL;

  if (epnum == (CDC_OUT_EP & 0x0FU))
  {
    uint32_t rx_len = USBD_LL_GetRxDataSize(pdev, CDC_OUT_EP);

    if (hcomp->cdc_fops != NULL && hcomp->cdc_fops->Receive != NULL)
    {
      hcomp->cdc_fops->Receive(hcomp->cdc_rx_buf, &rx_len);
    }

    USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcomp->cdc_rx_buf,
                            CDC_DATA_FS_MAX_PACKET_SIZE);
  }
  else if (epnum == (HID_OUT_EP & 0x0FU))
  {
    uint16_t rx_len = (uint16_t)USBD_LL_GetRxDataSize(pdev, HID_OUT_EP);

    if (hcomp->hid_rx_cb != NULL)
    {
      hcomp->hid_rx_cb(hcomp->hid_rx_buf, rx_len);
    }

    USBD_LL_PrepareReceive(pdev, HID_OUT_EP, hcomp->hid_rx_buf, HID_EPOUT_SIZE);
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_COMPOSITE_SOF(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return (uint8_t)USBD_OK;
}

/* ========================================================================== */
/*                          DESCRIPTORS                                        */
/* ========================================================================== */

static uint8_t *USBD_COMPOSITE_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_COMPOSITE_CfgDesc);
  return USBD_COMPOSITE_CfgDesc;
}

static uint8_t *USBD_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_COMPOSITE_DeviceQualifierDesc);
  return USBD_COMPOSITE_DeviceQualifierDesc;
}

/* ========================================================================== */
/*                          PUBLIC API                                          */
/* ========================================================================== */

uint8_t USBD_COMPOSITE_RegisterCDCInterface(USBD_HandleTypeDef *pdev,
                                             USBD_CDC_ItfTypeDef *fops)
{
  (void)pdev;
  hComposite.cdc_fops = fops;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_COMPOSITE_RegisterHIDCallback(USBD_HandleTypeDef *pdev,
                                            USBD_HID_ReceiveCallback_t cb)
{
  (void)pdev;
  hComposite.hid_rx_cb = cb;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_CDC_Transmit(USBD_HandleTypeDef *pdev,
                           uint8_t *buf, uint16_t len)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  if (hcomp == NULL) return (uint8_t)USBD_FAIL;

  if (hcomp->cdc_tx_busy != 0U) return (uint8_t)USBD_BUSY;

  hcomp->cdc_tx_busy = 1U;
  memcpy(hcomp->cdc_tx_buf, buf, MIN(len, CDC_DATA_FS_MAX_PACKET_SIZE));

  USBD_LL_Transmit(pdev, CDC_IN_EP, hcomp->cdc_tx_buf,
                    MIN(len, CDC_DATA_FS_MAX_PACKET_SIZE));

  return (uint8_t)USBD_OK;
}

uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev,
                              uint8_t *report, uint16_t len)
{
  USBD_COMPOSITE_HandleTypeDef *hcomp =
      (USBD_COMPOSITE_HandleTypeDef *)pdev->pClassData;

  if (hcomp == NULL) return (uint8_t)USBD_FAIL;

  if (hcomp->hid_tx_busy != 0U) return (uint8_t)USBD_BUSY;

  hcomp->hid_tx_busy = 1U;

  USBD_LL_Transmit(pdev, HID_IN_EP, report, MIN(len, HID_EPIN_SIZE));

  return (uint8_t)USBD_OK;
}
