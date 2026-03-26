/**
  ******************************************************************************
  * @file    regmap.c
  * @brief   Shared register map access layer for I2C / USB CDC / USB HID
  *
  * Extracted from main.c I2C_BuildTxBuffer() and HAL_I2C_SlaveRxCpltCallback()
  * so that multiple interfaces can read/write the same register map.
  ******************************************************************************
  */

#include "regmap.h"
#include "main.h"
#include <string.h>
#include "FreeRTOS.h"
#include "semphr.h"

/* ---- Register address definitions (mirrored from main.c USER CODE PD) ---- */

#define REG_PERIOD       0x00
#define REG_FREQ         0x04
#define REG_DUTY         0x08
#define REG_PULSE        0x0C
#define REG_EDGE         0x10
#define REG_TIM_PSC      0x11
#define REG_IC_PSC       0x13
#define REG_CAPTURE_CTRL 0x14

#define EDGE_RISING      0
#define EDGE_FALLING     1

#define REG_LED_PERIOD   0x20
#define REG_LED_DUTY     0x22
#define REG_LED_G_PERIOD 0x23
#define REG_LED_G_DUTY   0x25
#define REG_LED_R_PERIOD 0x26
#define REG_LED_R_DUTY   0x28

#define REG_SAVE_CFG     0x30
#define SAVE_CFG_KEY     0x5A

#define REG_PWM1_FREQ_L  0x40
#define REG_PWM1_FREQ_H  0x42
#define REG_PWM1_DUTY    0x44
#define REG_PWM1_CTRL    0x46
#define REG_PWM1_PSC     0x47
#define REG_PWM1_ARR     0x49

#define REG_PWM2_FREQ_L  0x4B
#define REG_PWM2_FREQ_H  0x4D
#define REG_PWM2_DUTY    0x4F
#define REG_PWM2_CTRL    0x51
#define REG_PWM2_PSC     0x52
#define REG_PWM2_ARR     0x54

#define REG_TRIG_WIDTH   0x56

#define REG_NICKNAME     0x60
#define NICKNAME_MAX_LEN 16

#define PWM_DUTY_MAX     10000U

/* ---- Extern globals (defined in main.c, no longer static) ---- */

/* Measurement registers (volatile, written by TIM2 ISR) */
extern volatile uint32_t g_period_ticks;
extern volatile uint32_t g_frequency_hz;
extern volatile uint32_t g_duty_centipct;
extern volatile uint32_t g_pulse_ticks;

/* Capture configuration */
extern uint8_t  g_edge_config;
extern uint16_t g_tim_psc;
extern uint8_t  g_ic_psc;
extern uint8_t  g_capture_enabled;

/* LED configuration */
extern uint16_t g_led_period_ms;
extern uint8_t  g_led_duty_pct;
extern uint16_t g_led_g_period_ms;
extern uint8_t  g_led_g_duty_pct;
extern uint16_t g_led_r_period_ms;
extern uint8_t  g_led_r_duty_pct;

/* PWM1 staging and active registers */
extern uint16_t g_pwm1_freq_l;
extern uint16_t g_pwm1_freq_h;
extern uint16_t g_pwm1_duty;
extern uint8_t  g_pwm1_ctrl;
extern uint16_t g_pwm1_psc;
extern uint16_t g_pwm1_arr;

/* PWM2 staging and active registers */
extern uint16_t g_pwm2_freq_l;
extern uint16_t g_pwm2_freq_h;
extern uint16_t g_pwm2_duty;
extern uint8_t  g_pwm2_ctrl;
extern uint16_t g_pwm2_psc;
extern uint16_t g_pwm2_arr;

/* Trigger config */
extern uint16_t g_trig_width_us;

/* Device nickname */
extern char g_nickname[];

/* ---- Extern side-effect functions (defined in main.c) ---- */

extern void FreqCounter_Reconfigure(void);
extern void PWM_Apply_Ext(uint8_t pwm_num);
extern void Config_Save(void);
extern void Trigger_Pulse(void);

/* ---- Private state ---- */

static SemaphoreHandle_t s_regmap_mutex = NULL;

/* ---- Public API ---- */

void RegMap_Init(void)
{
  s_regmap_mutex = xSemaphoreCreateMutex();
  configASSERT(s_regmap_mutex != NULL);
}

uint8_t RegMap_BuildSnapshot(uint8_t start_reg, uint8_t *buf, uint8_t buf_size)
{
  uint8_t map_bytes = (start_reg < REG_MAP_SIZE) ? (REG_MAP_SIZE - start_reg) : 1;
  if (buf_size < map_bytes) map_bytes = buf_size;

  /* Work on a local full-map image, then copy the requested window.
   * Stack cost: 112 bytes — acceptable for any task with >= 256-word stack. */
  uint8_t map[REG_MAP_SIZE];
  memset(map, 0, sizeof(map));

  /* 0x00-0x0F: measurement registers (snapshot volatile globals) */
  uint32_t period = g_period_ticks;
  uint32_t freq   = g_frequency_hz;
  uint32_t duty   = g_duty_centipct;
  uint32_t pulse  = g_pulse_ticks;
  memcpy(&map[0x00], &period, 4);
  memcpy(&map[0x04], &freq,   4);
  memcpy(&map[0x08], &duty,   4);
  memcpy(&map[0x0C], &pulse,  4);

  /* 0x10-0x13: capture config */
  map[0x10] = g_edge_config;
  memcpy(&map[0x11], &g_tim_psc, 2);
  map[0x13] = g_ic_psc;
  map[0x14] = g_capture_enabled;

  /* 0x15-0x1F: reserved (zeroed by memset) */

  /* 0x20-0x28: LED config */
  memcpy(&map[0x20], &g_led_period_ms,   2);
  map[0x22] = g_led_duty_pct;
  memcpy(&map[0x23], &g_led_g_period_ms, 2);
  map[0x25] = g_led_g_duty_pct;
  memcpy(&map[0x26], &g_led_r_period_ms, 2);
  map[0x28] = g_led_r_duty_pct;

  /* 0x29-0x3F: reserved (zeroed by memset) */

  /* 0x40-0x4A: PWM1 config */
  memcpy(&map[0x40], &g_pwm1_freq_l, 2);
  memcpy(&map[0x42], &g_pwm1_freq_h, 2);
  memcpy(&map[0x44], &g_pwm1_duty,   2);
  map[0x46] = g_pwm1_ctrl;
  memcpy(&map[0x47], &g_pwm1_psc, 2);
  memcpy(&map[0x49], &g_pwm1_arr, 2);

  /* 0x4B-0x55: PWM2 config */
  memcpy(&map[0x4B], &g_pwm2_freq_l, 2);
  memcpy(&map[0x4D], &g_pwm2_freq_h, 2);
  memcpy(&map[0x4F], &g_pwm2_duty,   2);
  map[0x51] = g_pwm2_ctrl;
  memcpy(&map[0x52], &g_pwm2_psc, 2);
  memcpy(&map[0x54], &g_pwm2_arr, 2);

  /* 0x56-0x57: trigger config */
  memcpy(&map[0x56], &g_trig_width_us, 2);

  /* 0x60-0x6F: device nickname */
  memcpy(&map[0x60], g_nickname, NICKNAME_MAX_LEN);

  /* Copy the requested window into caller's buffer */
  if (start_reg < REG_MAP_SIZE)
  {
    memcpy(buf, &map[start_reg], map_bytes);
  }
  else
  {
    memset(buf, 0, 1);
    map_bytes = 1;
  }

  return map_bytes;
}

uint8_t RegMap_Write(uint8_t reg_addr, const uint8_t *data, uint8_t data_len)
{
  if (data == NULL || data_len == 0) return 1;

  switch (reg_addr)
  {
  /* ---- Capture config ---- */
  case REG_EDGE:
    if (data[0] > EDGE_FALLING) return 2;
    if (data[0] != g_edge_config)
    {
      g_edge_config = data[0];
      FreqCounter_Reconfigure();
    }
    break;

  case REG_TIM_PSC:
  {
    if (data_len < 2) return 2;
    uint16_t new_psc;
    memcpy(&new_psc, data, 2);
    if (new_psc != g_tim_psc)
    {
      g_tim_psc = new_psc;
      FreqCounter_Reconfigure();
    }
    break;
  }

  case REG_IC_PSC:
    if (data[0] > 3) return 2;
    if (data[0] != g_ic_psc)
    {
      g_ic_psc = data[0];
      FreqCounter_Reconfigure();
    }
    break;

  case REG_CAPTURE_CTRL:
    if (data[0] > 1) return 2;
    if (data[0] != g_capture_enabled)
    {
      g_capture_enabled = data[0];
      FreqCounter_Reconfigure();
    }
    break;

  /* ---- LED config ---- */
  case REG_LED_PERIOD:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    if (val == 0) return 2;
    g_led_period_ms = val;
    break;
  }

  case REG_LED_DUTY:
    if (data[0] > 100) return 2;
    g_led_duty_pct = data[0];
    break;

  case REG_LED_G_PERIOD:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    if (val == 0) return 2;
    g_led_g_period_ms = val;
    break;
  }

  case REG_LED_G_DUTY:
    if (data[0] > 100) return 2;
    g_led_g_duty_pct = data[0];
    break;

  case REG_LED_R_PERIOD:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    if (val == 0) return 2;
    g_led_r_period_ms = val;
    break;
  }

  case REG_LED_R_DUTY:
    if (data[0] > 100) return 2;
    g_led_r_duty_pct = data[0];
    break;

  /* ---- Config save ---- */
  case REG_SAVE_CFG:
    if (data[0] == SAVE_CFG_KEY)
      Config_Save();
    break;

  /* ---- PWM1 registers ---- */
  case REG_PWM1_FREQ_L:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    g_pwm1_freq_l = val;
    break;
  }

  case REG_PWM1_FREQ_H:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    g_pwm1_freq_h = val;
    break;
  }

  case REG_PWM1_DUTY:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    if (val > PWM_DUTY_MAX) return 2;
    g_pwm1_duty = val;
    break;
  }

  case REG_PWM1_CTRL:
    g_pwm1_ctrl = data[0];
    PWM_Apply_Ext(1);
    Trigger_Pulse();
    break;

  /* ---- PWM2 registers ---- */
  case REG_PWM2_FREQ_L:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    g_pwm2_freq_l = val;
    break;
  }

  case REG_PWM2_FREQ_H:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    g_pwm2_freq_h = val;
    break;
  }

  case REG_PWM2_DUTY:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    if (val > PWM_DUTY_MAX) return 2;
    g_pwm2_duty = val;
    break;
  }

  case REG_PWM2_CTRL:
    g_pwm2_ctrl = data[0];
    PWM_Apply_Ext(2);
    Trigger_Pulse();
    break;

  /* ---- Trigger config ---- */
  case REG_TRIG_WIDTH:
  {
    if (data_len < 2) return 2;
    uint16_t val;
    memcpy(&val, data, 2);
    if (val < 1 || val > 1000) return 2;
    g_trig_width_us = val;
    break;
  }

  default:
    /* Byte-level writes within nickname region (0x60-0x6F) */
    if (reg_addr >= REG_NICKNAME && reg_addr < REG_NICKNAME + NICKNAME_MAX_LEN)
    {
      uint8_t offset = reg_addr - REG_NICKNAME;
      uint8_t max_copy = NICKNAME_MAX_LEN - offset;
      uint8_t copy_len = (data_len > max_copy) ? max_copy : data_len;
      memcpy(&g_nickname[offset], data, copy_len);
      /* Zero-pad remainder when writing from start */
      if (offset == 0 && copy_len < NICKNAME_MAX_LEN)
        memset(&g_nickname[copy_len], 0, NICKNAME_MAX_LEN - copy_len);
      g_nickname[NICKNAME_MAX_LEN] = '\0';
      break;
    }
    return 1;  /* unknown register */
  }

  return 0;
}

void RegMap_Lock(void)
{
  xSemaphoreTake(s_regmap_mutex, portMAX_DELAY);
}

void RegMap_Unlock(void)
{
  xSemaphoreGive(s_regmap_mutex);
}
