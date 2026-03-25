/**
  ******************************************************************************
  * @file    usbd_desc.c
  * @brief   USB device descriptors (device, string, LangID)
  ******************************************************************************
  */
#include "usbd_desc.h"
#include "usbd_core.h"

/* Private function prototypes */
static uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

/* Descriptor callback structure */
USBD_DescriptorsTypeDef FS_Desc = {
  USBD_FS_DeviceDescriptor,
  USBD_FS_LangIDStrDescriptor,
  USBD_FS_ManufacturerStrDescriptor,
  USBD_FS_ProductStrDescriptor,
  USBD_FS_SerialStrDescriptor,
  USBD_FS_ConfigStrDescriptor,
  USBD_FS_InterfaceStrDescriptor,
};

/* ---------- Device Descriptor --------------------------------------------- */
static uint8_t USBD_FS_DevDesc[USB_LEN_DEV_DESC] = {
  USB_LEN_DEV_DESC,                  /* bLength */
  USB_DESC_TYPE_DEVICE,              /* bDescriptorType */
  0x00, 0x02,                        /* bcdUSB: 2.00 */
  0xEF,                              /* bDeviceClass: Miscellaneous (IAD) */
  0x02,                              /* bDeviceSubClass: Common Class */
  0x01,                              /* bDeviceProtocol: IAD */
  USB_MAX_EP0_SIZE,                  /* bMaxPacketSize0 */
  LOBYTE(USBD_VID), HIBYTE(USBD_VID),    /* idVendor */
  LOBYTE(USBD_PID), HIBYTE(USBD_PID),    /* idProduct */
  LOBYTE(USBD_BCD_DEVICE), HIBYTE(USBD_BCD_DEVICE), /* bcdDevice */
  0x01,                              /* iManufacturer (string index 1) */
  0x02,                              /* iProduct (string index 2) */
  0x03,                              /* iSerialNumber (string index 3) */
  USBD_MAX_NUM_CONFIGURATION,        /* bNumConfigurations */
};

/* ---------- LangID Descriptor --------------------------------------------- */
static uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] = {
  USB_LEN_LANGID_STR_DESC,
  USB_DESC_TYPE_STRING,
  LOBYTE(USBD_LANGID_STRING),
  HIBYTE(USBD_LANGID_STRING),
};

/* ---------- String descriptor buffer -------------------------------------- */
static uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ];

/* ---------- Serial number from STM32 UID ---------------------------------- */

/**
  * @brief  Convert a 32-bit value to 8-char hex ASCII string (MSB first).
  */
static void IntToHex(uint32_t value, uint8_t *pbuf, uint8_t len)
{
  for (uint8_t i = 0U; i < len; i++)
  {
    uint8_t shift = (uint8_t)((len - 1U - i) * 4U);
    uint8_t nibble = (uint8_t)((value >> shift) & 0x0FU);

    pbuf[i] = (nibble < 0x0AU) ? (nibble + '0') : ((nibble - 0x0AU) + 'A');
  }
}

/**
  * @brief  Build serial number string from the 96-bit STM32 Unique ID.
  */
static void Get_SerialNum(uint8_t *serial_buf)
{
  /* STM32F4 Unique ID registers at 0x1FFF7A10 */
  uint32_t uid0 = *(uint32_t *)0x1FFF7A10U;
  uint32_t uid1 = *(uint32_t *)0x1FFF7A14U;
  uint32_t uid2 = *(uint32_t *)0x1FFF7A18U;

  /* Use XOR to mix down to two 32-bit values for a 16-char serial */
  uint32_t sn0 = uid0 + uid2;
  uint32_t sn1 = uid1;

  IntToHex(sn0, &serial_buf[0], 8U);
  IntToHex(sn1, &serial_buf[8], 8U);
  serial_buf[16] = '\0';
}

/* ---------- Descriptor callbacks ------------------------------------------ */

static uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = (uint16_t)sizeof(USBD_FS_DevDesc);
  return USBD_FS_DevDesc;
}

static uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = (uint16_t)sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

static uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_PRODUCT_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;

  static uint8_t serial_ascii[17];
  Get_SerialNum(serial_ascii);
  USBD_GetString(serial_ascii, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_INTERFACE_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}
