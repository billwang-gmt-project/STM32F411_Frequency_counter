/**
 ******************************************************************************
 * @file    config.h
 * @brief   Central device configuration — identifiers, version, USB IDs
 ******************************************************************************
 */
#ifndef CONFIG_H
#define CONFIG_H

/* USB identifiers */
#define CFG_USB_VID 0x0483U
#define CFG_USB_PID 0x5741U

/* Device identification */
#define CFG_MANUFACTURER "Winstrong Technology Co., Ltd."
#define CFG_MODEL "FC-411"

/* Firmware version — date-encoded: 0xYYMMDDnn (BCD-ish)
 * 0x26032600 = 2026-03-26, revision 00 */
#define CFG_FW_VERSION 0x26032600UL

#endif /* CONFIG_H */
