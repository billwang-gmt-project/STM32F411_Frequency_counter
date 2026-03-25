/**
  ******************************************************************************
  * @file    usbd_conf.c
  * @brief   USB Device low-level configuration (PCD / HAL glue layer)
  *          Implements USBD_LL_* functions and PCD callbacks.
  *          PCD handle + MspInit/MspDeInit are in CubeMX-generated usb_otg.c.
  ******************************************************************************
  */
#include "usbd_core.h"

/* PCD handle defined in Core/Src/usb_otg.c (CubeMX-generated) */
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* ========================================================================== */
/*                     PCD Callbacks -> USBD_LL dispatch                      */
/* ========================================================================== */

/**
  * @brief  Setup stage callback.
  */
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SetupStage((USBD_HandleTypeDef *)hpcd->pData,
                      (uint8_t *)hpcd->Setup);
}

/**
  * @brief  Data OUT stage callback.
  */
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataOutStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                        hpcd->OUT_ep[epnum].xfer_buff);
}

/**
  * @brief  Data IN stage callback.
  */
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataInStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                       hpcd->IN_ep[epnum].xfer_buff);
}

/**
  * @brief  SOF callback.
  */
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
}

/**
  * @brief  USB Reset callback.
  */
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_SpeedTypeDef speed = USBD_SPEED_FULL;

  switch (hpcd->Init.speed)
  {
    case PCD_SPEED_FULL:
      speed = USBD_SPEED_FULL;
      break;

    default:
      speed = USBD_SPEED_FULL;
      break;
  }

  USBD_LL_SetSpeed((USBD_HandleTypeDef *)hpcd->pData, speed);
  USBD_LL_Reset((USBD_HandleTypeDef *)hpcd->pData);
}

/**
  * @brief  Suspend callback.
  */
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_Suspend((USBD_HandleTypeDef *)hpcd->pData);

  /* Enter low-power mode if needed */
  if (hpcd->Init.low_power_enable)
  {
    SCB->SCR |= (uint32_t)((uint32_t)(SCB_SCR_SLEEPDEEP_Msk |
                                        SCB_SCR_SLEEPONEXIT_Msk));
  }
}

/**
  * @brief  Resume callback.
  */
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_Resume((USBD_HandleTypeDef *)hpcd->pData);
}

/**
  * @brief  Isochronous IN incomplete callback.
  */
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

/**
  * @brief  Isochronous OUT incomplete callback.
  */
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

/**
  * @brief  Connect callback.
  */
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevConnected((USBD_HandleTypeDef *)hpcd->pData);
}

/**
  * @brief  Disconnect callback.
  */
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}

/* ========================================================================== */
/*                     USBD_LL API Implementation                             */
/* ========================================================================== */

/**
  * @brief  Initialize the PCD (Peripheral Controller Driver).
  */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  /* Link the PCD handle (already initialized by MX_USB_OTG_FS_PCD_Init
     in CubeMX-generated usb_otg.c) to the USBD handle */
  hpcd_USB_OTG_FS.pData = pdev;
  pdev->pData = &hpcd_USB_OTG_FS;

  /* Configure FIFO allocation.
     Total OTG FS FIFO = 320 x 32-bit words = 1280 bytes.
     RxFIFO: shared among all OUT endpoints.
     TxFIFO0: EP0 IN.
     TxFIFO1: EP1 IN (CDC bulk data).
     TxFIFO2: EP2 IN (CDC CMD notification).
     TxFIFO3: EP3 IN (HID IN).

     Budget: 128 + 64 + 64 + 16 + 32 = 304 words (fits in 320). */
  HAL_PCD_SetRxFiFo(&hpcd_USB_OTG_FS,  128U);
  HAL_PCD_SetTxFiFo(&hpcd_USB_OTG_FS, 0,  64U);
  HAL_PCD_SetTxFiFo(&hpcd_USB_OTG_FS, 1,  64U);
  HAL_PCD_SetTxFiFo(&hpcd_USB_OTG_FS, 2,  16U);
  HAL_PCD_SetTxFiFo(&hpcd_USB_OTG_FS, 3,  32U);

  return USBD_OK;
}

/**
  * @brief  De-initialize the PCD.
  */
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_DeInit(pdev->pData);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Start the PCD (enable pull-up, connect).
  */
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_Start(pdev->pData);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Stop the PCD (disconnect).
  */
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_Stop(pdev->pData);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Open an endpoint.
  */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                   uint8_t ep_type, uint16_t ep_mps)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Close an endpoint.
  */
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_Close(pdev->pData, ep_addr);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Flush an endpoint.
  */
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_Flush(pdev->pData, ep_addr);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Set an endpoint to STALL state.
  */
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_SetStall(pdev->pData, ep_addr);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Clear STALL on an endpoint.
  */
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,
                                          uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Return true if endpoint is stalled.
  */
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;

  if ((ep_addr & 0x80U) != 0U)
  {
    return hpcd->IN_ep[ep_addr & 0x0FU].is_stall;
  }
  else
  {
    return hpcd->OUT_ep[ep_addr & 0x0FU].is_stall;
  }
}

/**
  * @brief  Set USB device address.
  */
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,
                                          uint8_t dev_addr)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_SetAddress(pdev->pData, dev_addr);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Transmit data on an IN endpoint.
  */
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                     uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Prepare to receive on an OUT endpoint.
  */
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
                                           uint8_t ep_addr,
                                           uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status;
  hal_status = HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);
  return (hal_status == HAL_OK) ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Get the last received data size on an endpoint.
  */
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

/**
  * @brief  Delay in ms.
  */
void USBD_LL_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}
