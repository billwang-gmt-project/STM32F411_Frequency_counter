/**
  * @file    usbd_ioreq.c
  * @brief   EP0 I/O request wrappers around USBD_LL_Transmit/Receive
  */
#include "usbd_ioreq.h"

USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *pdev,
                                     uint8_t *pbuf, uint32_t len)
{
  pdev->ep0_state          = USBD_EP0_DATA_IN;
  pdev->ep_in[0].total_length = len;
  pdev->ep_in[0].rem_length   = len;

  return USBD_LL_Transmit(pdev, 0x00U, pbuf, len);
}

USBD_StatusTypeDef USBD_CtlContinueSendData(USBD_HandleTypeDef *pdev,
                                              uint8_t *pbuf, uint32_t len)
{
  return USBD_LL_Transmit(pdev, 0x00U, pbuf, len);
}

USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *pdev,
                                      uint8_t *pbuf, uint32_t len)
{
  pdev->ep0_state           = USBD_EP0_DATA_OUT;
  pdev->ep_out[0].total_length = len;
  pdev->ep_out[0].rem_length   = len;

  return USBD_LL_PrepareReceive(pdev, 0x00U, pbuf, len);
}

USBD_StatusTypeDef USBD_CtlSendStatus(USBD_HandleTypeDef *pdev)
{
  pdev->ep0_state = USBD_EP0_STATUS_IN;
  return USBD_LL_Transmit(pdev, 0x00U, NULL, 0U);
}

USBD_StatusTypeDef USBD_CtlReceiveStatus(USBD_HandleTypeDef *pdev)
{
  pdev->ep0_state = USBD_EP0_STATUS_OUT;
  return USBD_LL_PrepareReceive(pdev, 0x00U, NULL, 0U);
}

uint32_t USBD_GetRxCount(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return USBD_LL_GetRxDataSize(pdev, ep_addr);
}
