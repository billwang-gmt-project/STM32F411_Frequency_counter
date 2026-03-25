/**
  ******************************************************************************
  * @file    usbd_desc.h
  * @brief   USB device descriptors header
  ******************************************************************************
  */
#ifndef __USBD_DESC_H
#define __USBD_DESC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_def.h"

/* Descriptor string indices */
#define USBD_LANGID_STRING            0x0409U   /* English (US) */
#define USBD_MANUFACTURER_STRING      "STMicroelectronics"
#define USBD_PRODUCT_STRING           "FC USB Composite"
#define USBD_CONFIGURATION_STRING     "Composite Config"
#define USBD_INTERFACE_STRING         "Composite Interface"

/* Device identifiers */
#define USBD_VID                      0x0483U
#define USBD_PID                      0x5741U
#define USBD_BCD_DEVICE               0x0200U

extern USBD_DescriptorsTypeDef FS_Desc;

#ifdef __cplusplus
}
#endif

#endif /* __USBD_DESC_H */
