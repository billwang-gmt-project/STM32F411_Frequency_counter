/**
  * @file    usbd_ctlreq.c
  * @brief   Standard USB control request handling
  */
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"

/* ---- Private helpers ---- */

static uint8_t USBD_GetLen(uint8_t *buf)
{
  uint8_t len = 0U;
  while (*buf != '\0') { len++; buf++; }
  return len;
}

static void USBD_GetDescriptor(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
  uint16_t len = 0U;
  uint8_t *pbuf = NULL;

  switch (req->wValue >> 8)
  {
  case USB_DESC_TYPE_DEVICE:
    pbuf = pdev->pDesc->GetDeviceDescriptor(pdev->dev_speed, &len);
    break;

  case USB_DESC_TYPE_CONFIGURATION:
    if (pdev->pClass != NULL && pdev->pClass->GetFSConfigDescriptor != NULL)
    {
      pbuf = pdev->pClass->GetFSConfigDescriptor(&len);
    }
    break;

  case USB_DESC_TYPE_STRING:
    switch ((uint8_t)(req->wValue))
    {
    case 0U: /* LangID */
      if (pdev->pDesc->GetLangIDStrDescriptor != NULL)
        pbuf = pdev->pDesc->GetLangIDStrDescriptor(pdev->dev_speed, &len);
      break;
    case 1U: /* Manufacturer */
      if (pdev->pDesc->GetManufacturerStrDescriptor != NULL)
        pbuf = pdev->pDesc->GetManufacturerStrDescriptor(pdev->dev_speed, &len);
      break;
    case 2U: /* Product */
      if (pdev->pDesc->GetProductStrDescriptor != NULL)
        pbuf = pdev->pDesc->GetProductStrDescriptor(pdev->dev_speed, &len);
      break;
    case 3U: /* Serial */
      if (pdev->pDesc->GetSerialStrDescriptor != NULL)
        pbuf = pdev->pDesc->GetSerialStrDescriptor(pdev->dev_speed, &len);
      break;
    case 4U: /* Configuration string */
      if (pdev->pDesc->GetConfigurationStrDescriptor != NULL)
        pbuf = pdev->pDesc->GetConfigurationStrDescriptor(pdev->dev_speed, &len);
      break;
    case 5U: /* Interface string */
      if (pdev->pDesc->GetInterfaceStrDescriptor != NULL)
        pbuf = pdev->pDesc->GetInterfaceStrDescriptor(pdev->dev_speed, &len);
      break;
    default:
      USBD_CtlError(pdev, req);
      return;
    }
    break;

  case USB_DESC_TYPE_DEVICE_QUALIFIER:
    if (pdev->pClass != NULL && pdev->pClass->GetDeviceQualifierDescriptor != NULL)
    {
      pbuf = pdev->pClass->GetDeviceQualifierDescriptor(&len);
    }
    else
    {
      USBD_CtlError(pdev, req);
      return;
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    return;
  }

  if (pbuf != NULL && len != 0U)
  {
    len = MIN(len, req->wLength);
    USBD_CtlSendData(pdev, pbuf, len);
  }
  else
  {
    USBD_CtlError(pdev, req);
  }
}

static void USBD_SetAddress(USBD_HandleTypeDef *pdev,
                             USBD_SetupReqTypedef *req)
{
  uint8_t dev_addr;

  if ((req->wIndex == 0U) && (req->wLength == 0U) && (req->wValue < 128U))
  {
    dev_addr = (uint8_t)(req->wValue) & 0x7FU;

    if (pdev->dev_state == USBD_STATE_CONFIGURED)
    {
      USBD_CtlError(pdev, req);
      return;
    }

    pdev->dev_address = dev_addr;
    USBD_LL_SetUSBAddress(pdev, dev_addr);
    USBD_CtlSendStatus(pdev);

    pdev->dev_state = (dev_addr != 0U) ?
                        USBD_STATE_ADDRESSED : USBD_STATE_DEFAULT;
  }
  else
  {
    USBD_CtlError(pdev, req);
  }
}

static void USBD_SetConfig(USBD_HandleTypeDef *pdev,
                            USBD_SetupReqTypedef *req)
{
  uint8_t cfgidx = (uint8_t)(req->wValue);

  if (cfgidx > USBD_MAX_NUM_CONFIGURATION)
  {
    USBD_CtlError(pdev, req);
    return;
  }

  switch (pdev->dev_state)
  {
  case USBD_STATE_ADDRESSED:
    if (cfgidx != 0U)
    {
      pdev->dev_config = cfgidx;
      pdev->dev_state  = USBD_STATE_CONFIGURED;

      if (pdev->pClass != NULL && pdev->pClass->Init != NULL)
      {
        if (pdev->pClass->Init(pdev, cfgidx) != 0U)
        {
          USBD_CtlError(pdev, req);
          return;
        }
      }
    }
    USBD_CtlSendStatus(pdev);
    break;

  case USBD_STATE_CONFIGURED:
    if (cfgidx == 0U)
    {
      pdev->dev_state  = USBD_STATE_ADDRESSED;
      pdev->dev_config = cfgidx;
      if (pdev->pClass != NULL && pdev->pClass->DeInit != NULL)
      {
        pdev->pClass->DeInit(pdev, cfgidx);
      }
    }
    else if (cfgidx != pdev->dev_config)
    {
      /* De-init old config */
      if (pdev->pClass != NULL && pdev->pClass->DeInit != NULL)
      {
        pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);
      }
      pdev->dev_config = cfgidx;
      if (pdev->pClass != NULL && pdev->pClass->Init != NULL)
      {
        if (pdev->pClass->Init(pdev, cfgidx) != 0U)
        {
          USBD_CtlError(pdev, req);
          return;
        }
      }
    }
    USBD_CtlSendStatus(pdev);
    break;

  default:
    USBD_CtlError(pdev, req);
    if (pdev->pClass != NULL && pdev->pClass->DeInit != NULL)
    {
      pdev->pClass->DeInit(pdev, cfgidx);
    }
    break;
  }
}

static void USBD_GetConfig(USBD_HandleTypeDef *pdev,
                            USBD_SetupReqTypedef *req)
{
  if (req->wLength != 1U)
  {
    USBD_CtlError(pdev, req);
    return;
  }

  switch (pdev->dev_state)
  {
  case USBD_STATE_DEFAULT:
  case USBD_STATE_ADDRESSED:
    pdev->dev_default_config = 0U;
    USBD_CtlSendData(pdev, (uint8_t *)&pdev->dev_default_config, 1U);
    break;

  case USBD_STATE_CONFIGURED:
    USBD_CtlSendData(pdev, (uint8_t *)&pdev->dev_config, 1U);
    break;

  default:
    USBD_CtlError(pdev, req);
    break;
  }
}

static void USBD_GetStatus(USBD_HandleTypeDef *pdev,
                            USBD_SetupReqTypedef *req)
{
  switch (pdev->dev_state)
  {
  case USBD_STATE_DEFAULT:
  case USBD_STATE_ADDRESSED:
  case USBD_STATE_CONFIGURED:
    if (req->wLength != 2U)
    {
      USBD_CtlError(pdev, req);
      return;
    }
    pdev->dev_config_status = (USBD_SELF_POWERED != 0U) ? 0x0001U : 0x0000U;
    if (pdev->dev_remote_wakeup)
    {
      pdev->dev_config_status |= 0x0002U;
    }
    USBD_CtlSendData(pdev, (uint8_t *)&pdev->dev_config_status, 2U);
    break;

  default:
    USBD_CtlError(pdev, req);
    break;
  }
}

/* ---- Public API ---- */

USBD_StatusTypeDef USBD_StdDevReq(USBD_HandleTypeDef *pdev,
                                   USBD_SetupReqTypedef *req)
{
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS:
  case USB_REQ_TYPE_VENDOR:
    if (pdev->pClass != NULL && pdev->pClass->Setup != NULL)
    {
      pdev->pClass->Setup(pdev, req);
    }
    break;

  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
    case USB_REQ_GET_DESCRIPTOR:
      USBD_GetDescriptor(pdev, req);
      break;

    case USB_REQ_SET_ADDRESS:
      USBD_SetAddress(pdev, req);
      break;

    case USB_REQ_SET_CONFIGURATION:
      USBD_SetConfig(pdev, req);
      break;

    case USB_REQ_GET_CONFIGURATION:
      USBD_GetConfig(pdev, req);
      break;

    case USB_REQ_GET_STATUS:
      USBD_GetStatus(pdev, req);
      break;

    case USB_REQ_SET_FEATURE:
      if (req->wValue == USB_FEATURE_REMOTE_WAKEUP)
      {
        pdev->dev_remote_wakeup = 1U;
        USBD_CtlSendStatus(pdev);
      }
      break;

    case USB_REQ_CLEAR_FEATURE:
      if (req->wValue == USB_FEATURE_REMOTE_WAKEUP)
      {
        pdev->dev_remote_wakeup = 0U;
        USBD_CtlSendStatus(pdev);
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      break;
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    break;
  }

  return ret;
}

USBD_StatusTypeDef USBD_StdItfReq(USBD_HandleTypeDef *pdev,
                                   USBD_SetupReqTypedef *req)
{
  USBD_StatusTypeDef ret = USBD_OK;

  switch (pdev->dev_state)
  {
  case USBD_STATE_DEFAULT:
  case USBD_STATE_ADDRESSED:
  case USBD_STATE_CONFIGURED:
    if (LOBYTE(req->wIndex) <= USBD_MAX_NUM_INTERFACES)
    {
      if (pdev->pClass != NULL && pdev->pClass->Setup != NULL)
      {
        ret = (USBD_StatusTypeDef)pdev->pClass->Setup(pdev, req);
      }

      if ((req->wLength == 0U) && (ret == USBD_OK))
      {
        USBD_CtlSendStatus(pdev);
      }
    }
    else
    {
      USBD_CtlError(pdev, req);
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    break;
  }

  return ret;
}

USBD_StatusTypeDef USBD_StdEPReq(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req)
{
  USBD_EndpointTypeDef *pep;
  uint8_t ep_addr = LOBYTE(req->wIndex);
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t status = 0x0000U;

  /* Determine IN or OUT endpoint */
  if ((ep_addr & 0x80U) != 0U)
    pep = &pdev->ep_in[ep_addr & 0x0FU];
  else
    pep = &pdev->ep_out[ep_addr & 0x0FU];

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS:
    if (pdev->pClass != NULL && pdev->pClass->Setup != NULL)
    {
      pdev->pClass->Setup(pdev, req);
    }
    break;

  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
    case USB_REQ_SET_FEATURE:
      if (pdev->dev_state == USBD_STATE_ADDRESSED ||
          pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
        {
          USBD_LL_StallEP(pdev, ep_addr);
        }
        else
        {
          USBD_CtlError(pdev, req);
        }
      }
      USBD_CtlSendStatus(pdev);
      break;

    case USB_REQ_CLEAR_FEATURE:
      if (pdev->dev_state == USBD_STATE_ADDRESSED ||
          pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
        {
          USBD_LL_ClearStallEP(pdev, ep_addr);
        }
        USBD_CtlSendStatus(pdev);
      }
      break;

    case USB_REQ_GET_STATUS:
      if (pdev->dev_state == USBD_STATE_ADDRESSED ||
          pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        if (USBD_LL_IsStallEP(pdev, ep_addr))
        {
          status = 0x0001U;
        }
        USBD_CtlSendData(pdev, (uint8_t *)&status, 2U);
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      break;
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    break;
  }

  (void)pep;
  return ret;
}

void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  (void)req;
  USBD_LL_StallEP(pdev, 0x80U);
  USBD_LL_StallEP(pdev, 0x00U);
}

void USBD_ParseSetupRequest(USBD_SetupReqTypedef *req, uint8_t *pdata)
{
  req->bmRequest = *(uint8_t *)(pdata);
  req->bRequest  = *(uint8_t *)(pdata + 1U);
  req->wValue    = SWAPBYTE(pdata + 2U);
  req->wIndex    = SWAPBYTE(pdata + 4U);
  req->wLength   = SWAPBYTE(pdata + 6U);
}

void USBD_GetString(uint8_t *desc, uint8_t *unicode, uint16_t *len)
{
  uint8_t idx = 0U;

  if (desc == NULL)
  {
    *len = 0U;
    return;
  }

  *len = (uint16_t)(USBD_GetLen(desc) * 2U + 2U);
  unicode[idx++] = (uint8_t)*len;
  unicode[idx++] = USB_DESC_TYPE_STRING;

  while (*desc != '\0')
  {
    unicode[idx++] = *desc++;
    unicode[idx++] = 0U;
  }
}
