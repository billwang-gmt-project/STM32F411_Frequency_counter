/**
  ******************************************************************************
  * @file    usbd_conf.h
  * @brief   USB Device configuration header (application layer)
  *          Included by usbd_def.h before type definitions.
  *          Must NOT include usbd_def.h (circular).
  ******************************************************************************
  */
#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------- USB Device configuration defines ------------------------- */
#define USBD_MAX_NUM_INTERFACES        4U
#define USBD_MAX_NUM_CONFIGURATION     1U
#define USBD_MAX_STR_DESC_SIZ         256U
#define USBD_SELF_POWERED              1U
#define USBD_DEBUG_LEVEL               0U
#define USBD_MAX_NUM_CLASSES           1U

/* EP0 max packet size */
#define USB_MAX_EP0_SIZE               64U

/* Memory management: static allocation (no malloc) */
#define USBD_malloc(x)                ((void *)0)  /* Not used */
#define USBD_free(x)                  ((void)0)
#define USBD_memset                   memset
#define USBD_memcpy                   memcpy
#define USBD_Delay                    HAL_Delay

/* Debug macros */
#if (USBD_DEBUG_LEVEL > 0U)
#define USBD_UsrLog(...)   do { printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_UsrLog(...)   do {} while (0)
#endif

#if (USBD_DEBUG_LEVEL > 1U)
#define USBD_ErrLog(...)   do { printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_ErrLog(...)   do {} while (0)
#endif

#if (USBD_DEBUG_LEVEL > 2U)
#define USBD_DbgLog(...)   do { printf("DEBUG: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_DbgLog(...)   do {} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF_H */
