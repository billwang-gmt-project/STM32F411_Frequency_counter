/**
  * @file    usbd_core.c
  * @brief   USB Device core state machine
  */
#include "usbd_core.h"

/* ---- Core API ---- */

USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *pdev,
                              USBD_DescriptorsTypeDef *pdesc, uint8_t id)
{
  if (pdev == NULL) return USBD_FAIL;

  pdev->dev_state   = USBD_STATE_DEFAULT;
  pdev->id          = id;
  pdev->pDesc       = pdesc;
  pdev->pClass      = NULL;
  pdev->pClassData  = NULL;
  pdev->pUserData   = NULL;
  pdev->dev_config  = 0U;
  pdev->dev_address = 0U;
  pdev->dev_connection_status = 0U;
  pdev->dev_remote_wakeup = 0U;
  pdev->ep0_state   = USBD_EP0_IDLE;
  pdev->ep0_data_len = 0U;

  /* Initialize the low-level driver */
  USBD_LL_Init(pdev);

  return USBD_OK;
}

USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *pdev)
{
  if (pdev == NULL) return USBD_FAIL;

  pdev->dev_state = USBD_STATE_DEFAULT;

  if (pdev->pClass != NULL && pdev->pClass->DeInit != NULL)
  {
    pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);
  }

  USBD_LL_Stop(pdev);
  USBD_LL_DeInit(pdev);

  return USBD_OK;
}

USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *pdev,
                                       USBD_ClassTypeDef *pclass)
{
  if (pclass == NULL) return USBD_FAIL;
  pdev->pClass = pclass;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *pdev)
{
  return USBD_LL_Start(pdev);
}

USBD_StatusTypeDef USBD_Stop(USBD_HandleTypeDef *pdev)
{
  if (pdev->pClass != NULL && pdev->pClass->DeInit != NULL)
  {
    pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);
  }
  return USBD_LL_Stop(pdev);
}

/* ---- LL Callbacks ---- */

USBD_StatusTypeDef USBD_LL_SetupStage(USBD_HandleTypeDef *pdev,
                                       uint8_t *psetup)
{
  USBD_ParseSetupRequest(&pdev->request, psetup);

  pdev->ep0_state   = USBD_EP0_SETUP;
  pdev->ep0_data_len = pdev->request.wLength;

  switch (pdev->request.bmRequest & 0x1FU)
  {
  case USB_REQ_RECIPIENT_DEVICE:
    USBD_StdDevReq(pdev, &pdev->request);
    break;

  case USB_REQ_RECIPIENT_INTERFACE:
    USBD_StdItfReq(pdev, &pdev->request);
    break;

  case USB_REQ_RECIPIENT_ENDPOINT:
    USBD_StdEPReq(pdev, &pdev->request);
    break;

  default:
    USBD_LL_StallEP(pdev, (pdev->request.bmRequest & 0x80U));
    break;
  }

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DataOutStage(USBD_HandleTypeDef *pdev,
                                         uint8_t epnum, uint8_t *pdata)
{
  USBD_EndpointTypeDef *pep;

  if (epnum == 0U)
  {
    pep = &pdev->ep_out[0];

    if (pdev->ep0_state == USBD_EP0_DATA_OUT)
    {
      if (pep->rem_length > pep->maxpacket)
      {
        pep->rem_length -= pep->maxpacket;
        USBD_CtlContinueSendData(pdev, pdata,
                                  (uint32_t)MIN(pep->rem_length, pep->maxpacket));
      }
      else
      {
        if (pdev->pClass != NULL && pdev->pClass->EP0_RxReady != NULL &&
            pdev->dev_state == USBD_STATE_CONFIGURED)
        {
          pdev->pClass->EP0_RxReady(pdev);
        }
        USBD_CtlSendStatus(pdev);
      }
    }
    else if (pdev->ep0_state == USBD_EP0_STATUS_OUT)
    {
      pdev->ep0_state = USBD_EP0_IDLE;
      USBD_LL_StallEP(pdev, 0x00U);
    }
  }
  else
  {
    if (pdev->pClass != NULL && pdev->pClass->DataOut != NULL &&
        pdev->dev_state == USBD_STATE_CONFIGURED)
    {
      pdev->pClass->DataOut(pdev, epnum);
    }
  }

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DataInStage(USBD_HandleTypeDef *pdev,
                                        uint8_t epnum, uint8_t *pdata)
{
  USBD_EndpointTypeDef *pep;

  if (epnum == 0U)
  {
    pep = &pdev->ep_in[0];

    if (pdev->ep0_state == USBD_EP0_DATA_IN)
    {
      if (pep->rem_length > pep->maxpacket)
      {
        pep->rem_length -= pep->maxpacket;
        USBD_CtlContinueSendData(pdev, pdata, pep->rem_length);

        /* Prepare EP0 OUT for early status phase from host */
        USBD_LL_PrepareReceive(pdev, 0U, NULL, 0U);
      }
      else
      {
        /* Last packet — check if ZLP is needed */
        if ((pep->total_length % pep->maxpacket == 0U) &&
            (pep->total_length >= pep->maxpacket) &&
            (pep->total_length < pdev->ep0_data_len))
        {
          USBD_CtlContinueSendData(pdev, NULL, 0U);
          pdev->ep0_data_len = 0U;

          USBD_LL_PrepareReceive(pdev, 0U, NULL, 0U);
        }
        else
        {
          if (pdev->pClass != NULL && pdev->pClass->EP0_TxSent != NULL &&
              pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            pdev->pClass->EP0_TxSent(pdev);
          }
          USBD_CtlReceiveStatus(pdev);
        }
      }
    }
    else if (pdev->ep0_state == USBD_EP0_STATUS_IN)
    {
      pdev->ep0_state = USBD_EP0_IDLE;
    }
  }
  else
  {
    if (pdev->pClass != NULL && pdev->pClass->DataIn != NULL &&
        pdev->dev_state == USBD_STATE_CONFIGURED)
    {
      pdev->pClass->DataIn(pdev, epnum);
    }
  }

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Reset(USBD_HandleTypeDef *pdev)
{
  pdev->dev_state   = USBD_STATE_DEFAULT;
  pdev->ep0_state   = USBD_EP0_IDLE;
  pdev->dev_address = 0U;
  pdev->dev_config  = 0U;
  pdev->dev_remote_wakeup = 0U;

  if (pdev->pClassData != NULL && pdev->pClass != NULL &&
      pdev->pClass->DeInit != NULL)
  {
    pdev->pClass->DeInit(pdev, 0U);
  }

  /* Open EP0 */
  USBD_LL_OpenEP(pdev, 0x00U, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
  pdev->ep_out[0].is_used    = 1U;
  pdev->ep_out[0].maxpacket  = USB_MAX_EP0_SIZE;

  USBD_LL_OpenEP(pdev, 0x80U, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
  pdev->ep_in[0].is_used     = 1U;
  pdev->ep_in[0].maxpacket   = USB_MAX_EP0_SIZE;

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_HandleTypeDef *pdev,
                                     USBD_SpeedTypeDef speed)
{
  pdev->dev_speed = speed;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Suspend(USBD_HandleTypeDef *pdev)
{
  pdev->dev_old_state = pdev->dev_state;
  pdev->dev_state     = USBD_STATE_SUSPENDED;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Resume(USBD_HandleTypeDef *pdev)
{
  if (pdev->dev_state == USBD_STATE_SUSPENDED)
  {
    pdev->dev_state = pdev->dev_old_state;
  }
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_SOF(USBD_HandleTypeDef *pdev)
{
  if (pdev->pClass != NULL && pdev->pClass->SOF != NULL &&
      pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    pdev->pClass->SOF(pdev);
  }
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_IsoINIncomplete(USBD_HandleTypeDef *pdev,
                                             uint8_t epnum)
{
  if (pdev->pClass != NULL && pdev->pClass->IsoINIncomplete != NULL)
  {
    pdev->pClass->IsoINIncomplete(pdev, epnum);
  }
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(USBD_HandleTypeDef *pdev,
                                              uint8_t epnum)
{
  if (pdev->pClass != NULL && pdev->pClass->IsoOUTIncomplete != NULL)
  {
    pdev->pClass->IsoOUTIncomplete(pdev, epnum);
  }
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DevConnected(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DevDisconnected(USBD_HandleTypeDef *pdev)
{
  pdev->dev_state = USBD_STATE_DEFAULT;
  if (pdev->pClass != NULL && pdev->pClass->DeInit != NULL)
  {
    pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);
  }
  return USBD_OK;
}
