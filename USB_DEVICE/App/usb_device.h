/**
  ******************************************************************************
  * @file    usb_device.h
  * @brief   USB Device initialization header
  ******************************************************************************
  */
#ifndef __USB_DEVICE_H
#define __USB_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_def.h"

/* Global USB device handle */
extern USBD_HandleTypeDef hUsbDeviceFS;

/**
  * @brief  Initialize and start the USB device stack.
  *         Call this after SystemClock_Config and before starting the scheduler.
  */
void MX_USB_DEVICE_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_DEVICE_H */
