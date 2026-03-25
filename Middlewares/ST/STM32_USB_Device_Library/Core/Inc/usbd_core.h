/**
  * @file    usbd_core.h
  * @brief   USB Device core API and LL callback prototypes
  */
#ifndef __USBD_CORE_H
#define __USBD_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_def.h"
#include "usbd_ioreq.h"
#include "usbd_ctlreq.h"

#ifndef DEVICE_FS
#define DEVICE_FS 0U
#endif

/* ---- Core API ---- */
USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *pdev,
                              USBD_DescriptorsTypeDef *pdesc, uint8_t id);
USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_Stop(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *pdev,
                                       USBD_ClassTypeDef *pclass);

/* ---- LL Callbacks (called from PCD callbacks in usbd_conf.c) ---- */
USBD_StatusTypeDef USBD_LL_SetupStage(USBD_HandleTypeDef *pdev, uint8_t *psetup);
USBD_StatusTypeDef USBD_LL_DataOutStage(USBD_HandleTypeDef *pdev, uint8_t epnum,
                                         uint8_t *pdata);
USBD_StatusTypeDef USBD_LL_DataInStage(USBD_HandleTypeDef *pdev, uint8_t epnum,
                                        uint8_t *pdata);
USBD_StatusTypeDef USBD_LL_Reset(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_HandleTypeDef *pdev,
                                     USBD_SpeedTypeDef speed);
USBD_StatusTypeDef USBD_LL_Suspend(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_Resume(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_SOF(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);
USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);
USBD_StatusTypeDef USBD_LL_DevConnected(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_LL_DevDisconnected(USBD_HandleTypeDef *pdev);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CORE_H */
