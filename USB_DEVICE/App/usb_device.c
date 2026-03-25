/**
  ******************************************************************************
  * @file    usb_device.c
  * @brief   USB Device initialization with CDC + HID callback registration
  ******************************************************************************
  */
#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_composite.h"
#include "cdc_fifo.h"
#include "main.h"

/* Global USB device handle */
USBD_HandleTypeDef hUsbDeviceFS;

/* --- CDC application callbacks --- */

static int8_t CDC_Init_FS(void)
{
  return 0;
}

static int8_t CDC_DeInit_FS(void)
{
  return 0;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  (void)cmd;
  (void)pbuf;
  (void)length;
  return 0;
}

static int8_t CDC_Receive_FS(uint8_t *buf, uint32_t *len)
{
  /* Push raw bytes into RX FIFO and wake UsbTask */
  CDC_RxPush(buf, (uint16_t)*len);
  CDC_RxNotifyFromISR();
  return 0;
}

static USBD_CDC_ItfTypeDef cdc_fops = {
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
};

/* --- HID application callback --- */

static int8_t HID_Receive_FS(uint8_t *buf, uint16_t len)
{
  /* Forward received HID OUT report to USB event queue (ISR-safe) */
  USB_EnqueueHidRx(buf, len);
  return 0;
}

/* --- USB Device Init --- */

void MX_USB_DEVICE_Init(void)
{
  /* Init USB device library, add supported class, start the library */
  USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
  USBD_RegisterClass(&hUsbDeviceFS, &USBD_COMPOSITE);

  /* Register CDC and HID application callbacks */
  USBD_COMPOSITE_RegisterCDCInterface(&hUsbDeviceFS, &cdc_fops);
  USBD_COMPOSITE_RegisterHIDCallback(&hUsbDeviceFS, HID_Receive_FS);

  USBD_Start(&hUsbDeviceFS);
}
