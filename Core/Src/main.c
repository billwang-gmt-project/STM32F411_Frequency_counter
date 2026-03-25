/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "usb_otg.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "regmap.h"
#include "usb_device.h"
#include "usbd_composite.h"
#include "usb_cdc_cmd.h"
#include "usb_hid_regmap.h"
#include "cdc_fifo.h"
#include "usbd_def.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TIM2_BASE_CLOCK_HZ  96000000UL
#define FREQ_TIMEOUT_MS     1000U

/* I2C register addresses */
#define REG_PERIOD       0x00   /* R   4 bytes: period in timer ticks */
#define REG_FREQ         0x04   /* R   4 bytes: frequency in Hz */
#define REG_DUTY         0x08   /* R   4 bytes: duty in 0.01% units */
#define REG_PULSE        0x0C   /* R   4 bytes: pulse width in ticks */
#define REG_EDGE         0x10   /* R/W 1 byte:  0=rising, 1=falling */
#define REG_TIM_PSC      0x11   /* R/W 2 bytes: timer prescaler (0-65535) */
#define REG_IC_PSC       0x13   /* R/W 1 byte:  IC prescaler (0-3) */

/* Edge configuration values */
#define EDGE_RISING      0
#define EDGE_FALLING     1

/* LEDs */
#define LED_GPIO_PORT       GPIOC
#define LED_GPIO_PIN        GPIO_PIN_13
#define LED_G_GPIO_PORT     GPIOC
#define LED_G_GPIO_PIN      GPIO_PIN_14
#define LED_R_GPIO_PORT     GPIOB
#define LED_R_GPIO_PIN      GPIO_PIN_10
#define LED_DEFAULT_PERIOD  1000U
#define LED_DEFAULT_DUTY    50U

/* I2C registers - LED (active-low status LED on PC13) */
#define REG_LED_PERIOD   0x20   /* R/W 2 bytes: LED blink period in ms */
#define REG_LED_DUTY     0x22   /* R/W 1 byte:  LED on-duty 0-100% */
/* I2C registers - LED_G (PC14) */
#define REG_LED_G_PERIOD 0x23   /* R/W 2 bytes: LED_G blink period in ms */
#define REG_LED_G_DUTY   0x25   /* R/W 1 byte:  LED_G on-duty 0-100% */
/* I2C registers - LED_R (PB10) */
#define REG_LED_R_PERIOD 0x26   /* R/W 2 bytes: LED_R blink period in ms */
#define REG_LED_R_DUTY   0x28   /* R/W 1 byte:  LED_R on-duty 0-100% */
/* I2C registers - config */
#define REG_SAVE_CFG     0x30   /* W   1 byte:  write 0x5A to save config */
#define SAVE_CFG_KEY     0x5A

/* I2C registers - PWM1 output (TIM1_CH1, PA8) */
#define REG_PWM1_FREQ_L  0x40   /* R/W 2 bytes: target freq low 16 bits (Hz) */
#define REG_PWM1_FREQ_H  0x42   /* R/W 2 bytes: target freq high 16 bits (Hz) */
#define REG_PWM1_DUTY    0x44   /* R/W 2 bytes: duty 0-10000 (0.01% units) */
#define REG_PWM1_CTRL    0x46   /* R/W 1 byte:  bit0=enable; write applies staged values */
#define REG_PWM1_PSC     0x47   /* R   2 bytes: auto-computed prescaler */
#define REG_PWM1_ARR     0x49   /* R   2 bytes: auto-computed ARR */
/* I2C registers - PWM2 output (TIM4_CH1, PB6) */
#define REG_PWM2_FREQ_L  0x4B   /* R/W 2 bytes: target freq low 16 bits (Hz) */
#define REG_PWM2_FREQ_H  0x4D   /* R/W 2 bytes: target freq high 16 bits (Hz) */
#define REG_PWM2_DUTY    0x4F   /* R/W 2 bytes: duty 0-10000 (0.01% units) */
#define REG_PWM2_CTRL    0x51   /* R/W 1 byte:  bit0=enable; write applies staged values */
#define REG_PWM2_PSC     0x52   /* R   2 bytes: auto-computed prescaler */
#define REG_PWM2_ARR     0x54   /* R   2 bytes: auto-computed ARR */
/* I2C registers - trigger config */
#define REG_TRIG_WIDTH   0x56   /* R/W 2 bytes: trigger pulse width in us (1-1000) */

/* PWM timer clock (TIM1 APB2, TIM4 APB1 — both 96 MHz with prescaler multiplier) */
#define PWM_TIMER_CLOCK_HZ  96000000UL
#define PWM_DEFAULT_DUTY     5000U   /* 50.00% */
#define PWM_DUTY_MAX         10000U

/* Trigger output (PA7) */
#define TRIG_GPIO_PORT  GPIOA
#define TRIG_GPIO_PIN   GPIO_PIN_7
#define TRIG_DEFAULT_WIDTH_US  10U

/* Flash config storage - Sector 7 (last 128KB sector of STM32F411CE) */
#define CONFIG_FLASH_SECTOR   FLASH_SECTOR_7
#define CONFIG_FLASH_ADDR     0x08060000UL
#define CONFIG_MAGIC          0xDEADBEF3UL
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t g_period_ticks = 0;
volatile uint32_t g_frequency_hz = 0;
volatile uint32_t g_duty_centipct = 0;   /* duty in 0.01% units (5000 = 50.00%) */
volatile uint32_t g_pulse_ticks = 0;
volatile uint32_t g_last_capture_tick = 0;

uint8_t  g_edge_config = EDGE_RISING;
uint16_t g_tim_psc = 0;
uint8_t  g_ic_psc = 0;           /* 0=DIV1, 1=DIV2, 2=DIV4, 3=DIV8 */

/* I2C register protocol state */
static uint8_t i2c_reg_addr = 0xFF;
static uint8_t i2c_rx_buf[3];           /* reg addr + up to 2 data bytes */
static uint8_t i2c_tx_buf[88];        /* full register map for burst reads (0x00-0x57) */

/* PWM output - timer handles */
static TIM_HandleTypeDef htim1_pwm;
static TIM_HandleTypeDef htim4_pwm;

/* PWM1 staging registers (written by I2C/USB, applied on CTRL write) */
uint16_t g_pwm1_freq_l = 0;
uint16_t g_pwm1_freq_h = 0;
uint16_t g_pwm1_duty = PWM_DEFAULT_DUTY;
uint8_t  g_pwm1_ctrl = 0;
/* PWM1 active computed values (read-only via I2C/USB) */
uint16_t g_pwm1_psc = 0;
uint16_t g_pwm1_arr = 0;

/* PWM2 staging registers */
uint16_t g_pwm2_freq_l = 0;
uint16_t g_pwm2_freq_h = 0;
uint16_t g_pwm2_duty = PWM_DEFAULT_DUTY;
uint8_t  g_pwm2_ctrl = 0;
/* PWM2 active computed values */
uint16_t g_pwm2_psc = 0;
uint16_t g_pwm2_arr = 0;

/* Trigger pulse config */
uint16_t g_trig_width_us = TRIG_DEFAULT_WIDTH_US;

/* LED parameters (written by I2C/USB, read by LedTask) */
uint16_t g_led_period_ms = LED_DEFAULT_PERIOD;
uint8_t  g_led_duty_pct = LED_DEFAULT_DUTY;
uint16_t g_led_g_period_ms = LED_DEFAULT_PERIOD;
uint8_t  g_led_g_duty_pct = LED_DEFAULT_DUTY;
uint16_t g_led_r_period_ms = LED_DEFAULT_PERIOD;
uint8_t  g_led_r_duty_pct = LED_DEFAULT_DUTY;

/* FreeRTOS */
#define LED_TASK_STACK_SIZE      256  /* words = 1024 bytes */
#define MONITOR_TASK_STACK_SIZE  256  /* words = 1024 bytes */
#define USB_TASK_STACK_SIZE      512  /* words = 2048 bytes */
static TaskHandle_t hLedTask = NULL;
static TaskHandle_t hMonitorTask = NULL;
static TaskHandle_t hUsbTask = NULL;

/* HID event queue (small, 64-byte reports — works fine with FreeRTOS queue) */
typedef struct {
  uint8_t data[64];
  uint16_t len;
} HidEvent_t;

#define HID_QUEUE_LEN  4
static QueueHandle_t hid_evt_queue = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void FreqCounter_Reconfigure(void);
static void LED_Init(void);
static void LED_G_Init(void);
static void LED_R_Init(void);
static void PWM1_Init(void);
static void PWM2_Init(void);
static void Trigger_GPIO_Init(void);
static uint8_t PWM_ComputeParams(uint32_t freq_hz, uint16_t *psc, uint16_t *arr);
static uint32_t PWM_ComputeCCR(uint16_t arr, uint16_t duty_centipct);
static void PWM_Apply(TIM_HandleTypeDef *htim, uint32_t channel,
                      uint32_t freq_hz, uint16_t duty_centipct,
                      uint8_t enable, uint16_t *out_psc, uint16_t *out_arr);
void Trigger_Pulse(void);
void Config_Save(void);
static void Config_Load(void);
static void LedTask(void *pvParameters);
static void MonitorTask(void *pvParameters);
static void UsbTask(void *pvParameters);
static uint8_t I2C_BuildTxBuffer(uint8_t start_reg);
void PWM_Apply_Ext(uint8_t pwm_num);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_USB_OTG_FS_PCD_Init();
  /* USER CODE BEGIN 2 */
  LED_Init();
  LED_G_Init();
  LED_R_Init();
  PWM1_Init();
  PWM2_Init();
  Trigger_GPIO_Init();
  Config_Load();

  HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);

  HAL_NVIC_SetPriority(I2C1_EV_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_SetPriority(I2C1_ER_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);

  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
  HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_2);
  HAL_I2C_EnableListen_IT(&hi2c1);

  /* Apply loaded config if non-default */
  if (g_edge_config != EDGE_RISING || g_tim_psc != 0 || g_ic_psc != 0)
  {
    FreqCounter_Reconfigure();
  }

  /* Apply saved PWM config if enabled */
  if (g_pwm1_ctrl & 0x01)
  {
    uint32_t freq = ((uint32_t)g_pwm1_freq_h << 16) | g_pwm1_freq_l;
    PWM_Apply(&htim1_pwm, TIM_CHANNEL_1, freq, g_pwm1_duty,
              1, &g_pwm1_psc, &g_pwm1_arr);
  }
  if (g_pwm2_ctrl & 0x01)
  {
    uint32_t freq = ((uint32_t)g_pwm2_freq_h << 16) | g_pwm2_freq_l;
    PWM_Apply(&htim4_pwm, TIM_CHANNEL_1, freq, g_pwm2_duty,
              1, &g_pwm2_psc, &g_pwm2_arr);
  }

  /* Initialize shared register access layer */
  RegMap_Init();

  /* Initialize CDC FIFOs (RX + TX ring buffers) */
  CDC_FIFO_Init();

  /* Initialize USB composite device (CDC + HID) */
  MX_USB_DEVICE_Init();

  /* HID event queue (CDC uses FIFO instead) */
  hid_evt_queue = xQueueCreate(HID_QUEUE_LEN, sizeof(HidEvent_t));

  /* Create FreeRTOS tasks */
  xTaskCreate(LedTask, "LED", LED_TASK_STACK_SIZE, NULL, 1, &hLedTask);
  xTaskCreate(MonitorTask, "MON", MONITOR_TASK_STACK_SIZE, NULL, 1, &hMonitorTask);
  xTaskCreate(UsbTask, "USB", USB_TASK_STACK_SIZE, NULL, 2, &hUsbTask);

  /* Start scheduler — does not return */
  vTaskStartScheduler();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Unreachable — scheduler is running */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* --- FreeRTOS Hooks --- */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  for (;;);
}

/* --- LED GPIO Init --- */

static void LED_Init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = LED_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_SET); /* off (active-low) */
}

static void LED_G_Init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = LED_G_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_G_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(LED_G_GPIO_PORT, LED_G_GPIO_PIN, GPIO_PIN_RESET); /* off */
}

static void LED_R_Init(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = LED_R_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_R_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(LED_R_GPIO_PORT, LED_R_GPIO_PIN, GPIO_PIN_RESET); /* off */
}

/* --- PWM Output Init --- */

static void PWM1_Init(void)
{
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(GPIOA, &gpio);

  htim1_pwm.Instance = TIM1;
  htim1_pwm.Init.Prescaler = 0;
  htim1_pwm.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1_pwm.Init.Period = 65535;
  htim1_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1_pwm.Init.RepetitionCounter = 0;
  htim1_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  HAL_TIM_PWM_Init(&htim1_pwm);

  TIM_OC_InitTypeDef oc = {0};
  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  oc.OCIdleState = TIM_OCIDLESTATE_RESET;
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  HAL_TIM_PWM_ConfigChannel(&htim1_pwm, &oc, TIM_CHANNEL_1);
}

static void PWM2_Init(void)
{
  __HAL_RCC_TIM4_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(GPIOB, &gpio);

  htim4_pwm.Instance = TIM4;
  htim4_pwm.Init.Prescaler = 0;
  htim4_pwm.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4_pwm.Init.Period = 65535;
  htim4_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4_pwm.Init.RepetitionCounter = 0;
  htim4_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  HAL_TIM_PWM_Init(&htim4_pwm);

  TIM_OC_InitTypeDef oc = {0};
  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  HAL_TIM_PWM_ConfigChannel(&htim4_pwm, &oc, TIM_CHANNEL_1);
}

static void Trigger_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = TRIG_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(TRIG_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(TRIG_GPIO_PORT, TRIG_GPIO_PIN, GPIO_PIN_RESET);
}

/* --- PWM Auto-Prescaler and Glitch-Free Apply --- */

static uint8_t PWM_ComputeParams(uint32_t freq_hz, uint16_t *psc, uint16_t *arr)
{
  if (freq_hz == 0) return 1;
  uint32_t total = PWM_TIMER_CLOCK_HZ / freq_hz;
  if (total < 2) return 1;
  uint32_t prescaler = (total > 65536) ? (total - 1) / 65536 : 0;
  if (prescaler > 65535) return 1;
  uint32_t period = total / (prescaler + 1) - 1;
  if (period > 65535) period = 65535;
  if (period == 0) period = 1;
  *psc = (uint16_t)prescaler;
  *arr = (uint16_t)period;
  return 0;
}

static uint32_t PWM_ComputeCCR(uint16_t arr, uint16_t duty_centipct)
{
  if (duty_centipct >= PWM_DUTY_MAX) return (uint32_t)arr + 1;
  if (duty_centipct == 0) return 0;
  return (uint32_t)(arr + 1) * duty_centipct / PWM_DUTY_MAX;
}

static void PWM_Apply(TIM_HandleTypeDef *htim, uint32_t channel,
                      uint32_t freq_hz, uint16_t duty_centipct,
                      uint8_t enable, uint16_t *out_psc, uint16_t *out_arr)
{
  if (!enable || freq_hz == 0)
  {
    HAL_TIM_PWM_Stop(htim, channel);
    *out_psc = 0;
    *out_arr = 0;
    return;
  }

  uint16_t psc, arr;
  if (PWM_ComputeParams(freq_hz, &psc, &arr) != 0)
  {
    *out_psc = 0;
    *out_arr = 0;
    return;
  }
  uint32_t ccr = PWM_ComputeCCR(arr, duty_centipct);

  __HAL_TIM_SET_PRESCALER(htim, psc);
  __HAL_TIM_SET_AUTORELOAD(htim, arr);
  __HAL_TIM_SET_COMPARE(htim, channel, ccr);
  htim->Instance->EGR = TIM_EGR_UG;

  *out_psc = psc;
  *out_arr = arr;

  if (!(htim->Instance->CR1 & TIM_CR1_CEN))
  {
    HAL_TIM_PWM_Start(htim, channel);
  }
}

void Trigger_Pulse(void)
{
  uint32_t loops = (uint32_t)g_trig_width_us * 24;  /* ~96MHz/4 cycles per loop */
  TRIG_GPIO_PORT->BSRR = TRIG_GPIO_PIN;
  for (volatile uint32_t i = 0; i < loops; i++) {}
  TRIG_GPIO_PORT->BSRR = (uint32_t)TRIG_GPIO_PIN << 16;
}

/* --- FreeRTOS Tasks --- */

static void LedTask(void *pvParameters)
{
  (void)pvParameters;
  TickType_t now = xTaskGetTickCount();
  uint8_t led_on = 0, led_g_on = 0, led_r_on = 0;
  TickType_t led_next = now, led_g_next = now, led_r_next = now;

  for (;;)
  {
    now = xTaskGetTickCount();

    /* Status LED (PC13, active-low) */
    if ((int32_t)(now - led_next) >= 0)
    {
      uint32_t on_time = (uint32_t)g_led_period_ms * g_led_duty_pct / 100;
      uint32_t off_time = g_led_period_ms - on_time;
      if (led_on) {
        HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_SET);
        led_on = 0;
        led_next = now + off_time;
      } else {
        HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_RESET);
        led_on = 1;
        led_next = now + on_time;
      }
    }

    /* Green LED (PC14, active-high) */
    if ((int32_t)(now - led_g_next) >= 0)
    {
      uint32_t on_time = (uint32_t)g_led_g_period_ms * g_led_g_duty_pct / 100;
      uint32_t off_time = g_led_g_period_ms - on_time;
      if (led_g_on) {
        HAL_GPIO_WritePin(LED_G_GPIO_PORT, LED_G_GPIO_PIN, GPIO_PIN_RESET);
        led_g_on = 0;
        led_g_next = now + off_time;
      } else {
        HAL_GPIO_WritePin(LED_G_GPIO_PORT, LED_G_GPIO_PIN, GPIO_PIN_SET);
        led_g_on = 1;
        led_g_next = now + on_time;
      }
    }

    /* Red LED (PB10, active-high) */
    if ((int32_t)(now - led_r_next) >= 0)
    {
      uint32_t on_time = (uint32_t)g_led_r_period_ms * g_led_r_duty_pct / 100;
      uint32_t off_time = g_led_r_period_ms - on_time;
      if (led_r_on) {
        HAL_GPIO_WritePin(LED_R_GPIO_PORT, LED_R_GPIO_PIN, GPIO_PIN_RESET);
        led_r_on = 0;
        led_r_next = now + off_time;
      } else {
        HAL_GPIO_WritePin(LED_R_GPIO_PORT, LED_R_GPIO_PIN, GPIO_PIN_SET);
        led_r_on = 1;
        led_r_next = now + on_time;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void MonitorTask(void *pvParameters)
{
  (void)pvParameters;

  for (;;)
  {
    if (HAL_GetTick() - g_last_capture_tick > FREQ_TIMEOUT_MS)
    {
      g_period_ticks = 0;
      g_frequency_hz = 0;
      g_duty_centipct = 0;
      g_pulse_ticks = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/* --- Flash Config Storage --- */

typedef struct {
  uint32_t magic;
  uint8_t  edge_config;
  uint16_t tim_psc;
  uint8_t  ic_psc;
  uint16_t led_period_ms;
  uint8_t  led_duty_pct;
  uint16_t led_g_period_ms;
  uint8_t  led_g_duty_pct;
  uint16_t led_r_period_ms;
  uint8_t  led_r_duty_pct;
  /* PWM outputs */
  uint16_t pwm1_freq_l;
  uint16_t pwm1_freq_h;
  uint16_t pwm1_duty;
  uint8_t  pwm1_ctrl;
  uint16_t pwm2_freq_l;
  uint16_t pwm2_freq_h;
  uint16_t pwm2_duty;
  uint8_t  pwm2_ctrl;
  uint16_t trig_width_us;
} ConfigData_t;

static void Config_Load(void)
{
  const ConfigData_t *cfg = (const ConfigData_t *)CONFIG_FLASH_ADDR;
  if (cfg->magic != CONFIG_MAGIC) return;

  g_edge_config     = cfg->edge_config <= EDGE_FALLING ? cfg->edge_config : EDGE_RISING;
  g_tim_psc         = cfg->tim_psc;
  g_ic_psc          = cfg->ic_psc <= 3 ? cfg->ic_psc : 0;
  g_led_period_ms   = cfg->led_period_ms > 0 ? cfg->led_period_ms : LED_DEFAULT_PERIOD;
  g_led_duty_pct    = cfg->led_duty_pct <= 100 ? cfg->led_duty_pct : LED_DEFAULT_DUTY;
  g_led_g_period_ms = cfg->led_g_period_ms > 0 ? cfg->led_g_period_ms : LED_DEFAULT_PERIOD;
  g_led_g_duty_pct  = cfg->led_g_duty_pct <= 100 ? cfg->led_g_duty_pct : LED_DEFAULT_DUTY;
  g_led_r_period_ms = cfg->led_r_period_ms > 0 ? cfg->led_r_period_ms : LED_DEFAULT_PERIOD;
  g_led_r_duty_pct  = cfg->led_r_duty_pct <= 100 ? cfg->led_r_duty_pct : LED_DEFAULT_DUTY;

  /* PWM outputs */
  g_pwm1_freq_l    = cfg->pwm1_freq_l;
  g_pwm1_freq_h    = cfg->pwm1_freq_h;
  g_pwm1_duty      = cfg->pwm1_duty <= PWM_DUTY_MAX ? cfg->pwm1_duty : PWM_DEFAULT_DUTY;
  g_pwm1_ctrl      = cfg->pwm1_ctrl;
  g_pwm2_freq_l    = cfg->pwm2_freq_l;
  g_pwm2_freq_h    = cfg->pwm2_freq_h;
  g_pwm2_duty      = cfg->pwm2_duty <= PWM_DUTY_MAX ? cfg->pwm2_duty : PWM_DEFAULT_DUTY;
  g_pwm2_ctrl      = cfg->pwm2_ctrl;
  g_trig_width_us  = (cfg->trig_width_us >= 1 && cfg->trig_width_us <= 1000)
                       ? cfg->trig_width_us : TRIG_DEFAULT_WIDTH_US;
}

void Config_Save(void)
{
  ConfigData_t cfg;
  cfg.magic           = CONFIG_MAGIC;
  cfg.edge_config     = g_edge_config;
  cfg.tim_psc         = g_tim_psc;
  cfg.ic_psc          = g_ic_psc;
  cfg.led_period_ms   = g_led_period_ms;
  cfg.led_duty_pct    = g_led_duty_pct;
  cfg.led_g_period_ms = g_led_g_period_ms;
  cfg.led_g_duty_pct  = g_led_g_duty_pct;
  cfg.led_r_period_ms = g_led_r_period_ms;
  cfg.led_r_duty_pct  = g_led_r_duty_pct;
  cfg.pwm1_freq_l     = g_pwm1_freq_l;
  cfg.pwm1_freq_h     = g_pwm1_freq_h;
  cfg.pwm1_duty       = g_pwm1_duty;
  cfg.pwm1_ctrl       = g_pwm1_ctrl;
  cfg.pwm2_freq_l     = g_pwm2_freq_l;
  cfg.pwm2_freq_h     = g_pwm2_freq_h;
  cfg.pwm2_duty       = g_pwm2_duty;
  cfg.pwm2_ctrl       = g_pwm2_ctrl;
  cfg.trig_width_us   = g_trig_width_us;

  HAL_FLASH_Unlock();

  FLASH_EraseInitTypeDef erase = {0};
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = CONFIG_FLASH_SECTOR;
  erase.NbSectors = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t error = 0;
  HAL_FLASHEx_Erase(&erase, &error);

  uint32_t *src = (uint32_t *)&cfg;
  uint32_t addr = CONFIG_FLASH_ADDR;
  for (uint32_t i = 0; i < sizeof(ConfigData_t) / 4; i++)
  {
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]);
    addr += 4;
  }

  HAL_FLASH_Lock();
}

/* --- Full timer reconfiguration (edge, prescalers) --- */

static const uint32_t ic_psc_map[] = {
    TIM_ICPSC_DIV1, TIM_ICPSC_DIV2, TIM_ICPSC_DIV4, TIM_ICPSC_DIV8
};

void FreqCounter_Reconfigure(void)
{
  HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
  HAL_TIM_IC_Stop(&htim2, TIM_CHANNEL_2);

  /* Timer prescaler */
  __HAL_TIM_SET_PRESCALER(&htim2, g_tim_psc);
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  htim2.Instance->EGR = TIM_EGR_UG;

  /* CH1: direct, period edge */
  TIM_IC_InitTypeDef sConfigIC = {0};
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = ic_psc_map[g_ic_psc & 0x03];
  sConfigIC.ICFilter = 0;

  /* CH2: indirect, opposite edge for pulse width */
  TIM_IC_InitTypeDef sConfigIC2 = {0};
  sConfigIC2.ICSelection = TIM_ICSELECTION_INDIRECTTI;
  sConfigIC2.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC2.ICFilter = 0;

  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
  sSlaveConfig.InputTrigger = TIM_TS_TI1FP1;
  sSlaveConfig.TriggerFilter = 0;

  if (g_edge_config == EDGE_FALLING)
  {
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    sConfigIC2.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sSlaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
  }
  else
  {
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC2.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    sSlaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  }

  HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_1);
  HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC2, TIM_CHANNEL_2);
  HAL_TIM_SlaveConfigSynchro(&htim2, &sSlaveConfig);

  /* Clear measurements and restart */
  g_period_ticks = 0;
  g_frequency_hz = 0;
  g_duty_centipct = 0;
  g_pulse_ticks = 0;

  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
  HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_2);
}

/* --- TIM2 Input Capture (PWM Input mode) --- */

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
  {
    uint32_t period = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1) + 1;
    uint32_t pulse  = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2) + 1;

    if (period > 1)
    {
      uint32_t timer_clock = TIM2_BASE_CLOCK_HZ / (g_tim_psc + 1);
      g_period_ticks = period;
      g_pulse_ticks = pulse;
      g_frequency_hz = timer_clock / period;
      g_duty_centipct = (uint32_t)((uint64_t)pulse * 10000 / period);
    }
    g_last_capture_tick = HAL_GetTick();
  }
}

/* --- I2C Slave Register Protocol --- */

#define REG_MAP_END  0x58  /* one past last register byte (TRIG_WIDTH at 0x56, 2 bytes) */

static uint8_t I2C_BuildTxBuffer(uint8_t start_reg)
{
  /* Build a contiguous register-map image in i2c_tx_buf.
   * Returns the number of bytes available from start_reg onward.
   * Master can read as many bytes as it wants in a burst. */
  memset(i2c_tx_buf, 0, sizeof(i2c_tx_buf));

  /* 0x00-0x0F: measurement registers (4 bytes each) */
  uint32_t period = g_period_ticks;
  uint32_t freq   = g_frequency_hz;
  uint32_t duty   = g_duty_centipct;
  uint32_t pulse  = g_pulse_ticks;
  memcpy(&i2c_tx_buf[0x00], &period, 4);
  memcpy(&i2c_tx_buf[0x04], &freq, 4);
  memcpy(&i2c_tx_buf[0x08], &duty, 4);
  memcpy(&i2c_tx_buf[0x0C], &pulse, 4);

  /* 0x10-0x13: capture config */
  i2c_tx_buf[0x10] = g_edge_config;
  memcpy(&i2c_tx_buf[0x11], &g_tim_psc, 2);
  i2c_tx_buf[0x13] = g_ic_psc;

  /* 0x14-0x1F: reserved (zeroed by memset) */

  /* 0x20-0x28: LED config */
  memcpy(&i2c_tx_buf[0x20], &g_led_period_ms, 2);
  i2c_tx_buf[0x22] = g_led_duty_pct;
  memcpy(&i2c_tx_buf[0x23], &g_led_g_period_ms, 2);
  i2c_tx_buf[0x25] = g_led_g_duty_pct;
  memcpy(&i2c_tx_buf[0x26], &g_led_r_period_ms, 2);
  i2c_tx_buf[0x28] = g_led_r_duty_pct;

  /* 0x29-0x3F: reserved (zeroed by memset) */

  /* 0x40-0x4A: PWM1 config */
  memcpy(&i2c_tx_buf[0x40], &g_pwm1_freq_l, 2);
  memcpy(&i2c_tx_buf[0x42], &g_pwm1_freq_h, 2);
  memcpy(&i2c_tx_buf[0x44], &g_pwm1_duty, 2);
  i2c_tx_buf[0x46] = g_pwm1_ctrl;
  memcpy(&i2c_tx_buf[0x47], &g_pwm1_psc, 2);
  memcpy(&i2c_tx_buf[0x49], &g_pwm1_arr, 2);

  /* 0x4B-0x55: PWM2 config */
  memcpy(&i2c_tx_buf[0x4B], &g_pwm2_freq_l, 2);
  memcpy(&i2c_tx_buf[0x4D], &g_pwm2_freq_h, 2);
  memcpy(&i2c_tx_buf[0x4F], &g_pwm2_duty, 2);
  i2c_tx_buf[0x51] = g_pwm2_ctrl;
  memcpy(&i2c_tx_buf[0x52], &g_pwm2_psc, 2);
  memcpy(&i2c_tx_buf[0x54], &g_pwm2_arr, 2);

  /* 0x56-0x57: trigger config */
  memcpy(&i2c_tx_buf[0x56], &g_trig_width_us, 2);

  if (start_reg >= REG_MAP_END) return 1; /* fallback: send 1 zero byte */
  return REG_MAP_END - start_reg;
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                           uint16_t AddrMatchCode)
{
  if (hi2c->Instance != I2C1) return;

  if (TransferDirection == I2C_DIRECTION_TRANSMIT)
  {
    /* Master is writing: receive reg addr + up to 2 data bytes */
    HAL_I2C_Slave_Seq_Receive_IT(hi2c, i2c_rx_buf, 3, I2C_FIRST_FRAME);
  }
  else /* I2C_DIRECTION_RECEIVE — master is reading */
  {
    /* Capture register address from the write phase (may not have triggered RxCplt) */
    i2c_reg_addr = i2c_rx_buf[0];

    /* Build register map snapshot from selected register onward */
    uint8_t len = I2C_BuildTxBuffer(i2c_reg_addr);
    HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &i2c_tx_buf[i2c_reg_addr < REG_MAP_END ? i2c_reg_addr : 0],
                                   len, I2C_LAST_FRAME);
  }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance != I2C1) return;

  i2c_reg_addr = i2c_rx_buf[0];
  uint8_t need_reconfig = 0;

  switch (i2c_reg_addr)
  {
  case REG_EDGE:
    if (i2c_rx_buf[1] <= EDGE_FALLING && i2c_rx_buf[1] != g_edge_config)
    {
      g_edge_config = i2c_rx_buf[1];
      need_reconfig = 1;
    }
    break;
  case REG_TIM_PSC:
  {
    uint16_t new_psc;
    memcpy(&new_psc, &i2c_rx_buf[1], 2);
    if (new_psc != g_tim_psc)
    {
      g_tim_psc = new_psc;
      need_reconfig = 1;
    }
    break;
  }
  case REG_IC_PSC:
    if (i2c_rx_buf[1] <= 3 && i2c_rx_buf[1] != g_ic_psc)
    {
      g_ic_psc = i2c_rx_buf[1];
      need_reconfig = 1;
    }
    break;
  case REG_LED_PERIOD:
  {
    uint16_t new_period;
    memcpy(&new_period, &i2c_rx_buf[1], 2);
    if (new_period > 0) g_led_period_ms = new_period;
    break;
  }
  case REG_LED_DUTY:
    if (i2c_rx_buf[1] <= 100) g_led_duty_pct = i2c_rx_buf[1];
    break;
  case REG_LED_G_PERIOD:
  {
    uint16_t new_period;
    memcpy(&new_period, &i2c_rx_buf[1], 2);
    if (new_period > 0) g_led_g_period_ms = new_period;
    break;
  }
  case REG_LED_G_DUTY:
    if (i2c_rx_buf[1] <= 100) g_led_g_duty_pct = i2c_rx_buf[1];
    break;
  case REG_LED_R_PERIOD:
  {
    uint16_t new_period;
    memcpy(&new_period, &i2c_rx_buf[1], 2);
    if (new_period > 0) g_led_r_period_ms = new_period;
    break;
  }
  case REG_LED_R_DUTY:
    if (i2c_rx_buf[1] <= 100) g_led_r_duty_pct = i2c_rx_buf[1];
    break;
  case REG_SAVE_CFG:
    if (i2c_rx_buf[1] == SAVE_CFG_KEY) Config_Save();
    break;

  /* --- PWM1 registers --- */
  case REG_PWM1_FREQ_L:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    g_pwm1_freq_l = val;
    break;
  }
  case REG_PWM1_FREQ_H:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    g_pwm1_freq_h = val;
    break;
  }
  case REG_PWM1_DUTY:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    if (val <= PWM_DUTY_MAX) g_pwm1_duty = val;
    break;
  }
  case REG_PWM1_CTRL:
  {
    g_pwm1_ctrl = i2c_rx_buf[1];
    uint32_t freq = ((uint32_t)g_pwm1_freq_h << 16) | g_pwm1_freq_l;
    PWM_Apply(&htim1_pwm, TIM_CHANNEL_1, freq, g_pwm1_duty,
              g_pwm1_ctrl & 0x01, &g_pwm1_psc, &g_pwm1_arr);
    Trigger_Pulse();
    break;
  }

  /* --- PWM2 registers --- */
  case REG_PWM2_FREQ_L:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    g_pwm2_freq_l = val;
    break;
  }
  case REG_PWM2_FREQ_H:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    g_pwm2_freq_h = val;
    break;
  }
  case REG_PWM2_DUTY:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    if (val <= PWM_DUTY_MAX) g_pwm2_duty = val;
    break;
  }
  case REG_PWM2_CTRL:
  {
    g_pwm2_ctrl = i2c_rx_buf[1];
    uint32_t freq = ((uint32_t)g_pwm2_freq_h << 16) | g_pwm2_freq_l;
    PWM_Apply(&htim4_pwm, TIM_CHANNEL_1, freq, g_pwm2_duty,
              g_pwm2_ctrl & 0x01, &g_pwm2_psc, &g_pwm2_arr);
    Trigger_Pulse();
    break;
  }

  /* --- Trigger config --- */
  case REG_TRIG_WIDTH:
  {
    uint16_t val;
    memcpy(&val, &i2c_rx_buf[1], 2);
    if (val >= 1 && val <= 1000) g_trig_width_us = val;
    break;
  }

  default:
    break;
  }

  if (need_reconfig)
  {
    FreqCounter_Reconfigure();
  }
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
  HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  HAL_I2C_EnableListen_IT(hi2c);
}

/* --- PWM_Apply_Ext: wrapper for regmap.c to call --- */

void PWM_Apply_Ext(uint8_t pwm_num)
{
  if (pwm_num == 1)
  {
    uint32_t freq = ((uint32_t)g_pwm1_freq_h << 16) | g_pwm1_freq_l;
    PWM_Apply(&htim1_pwm, TIM_CHANNEL_1, freq, g_pwm1_duty,
              g_pwm1_ctrl & 0x01, &g_pwm1_psc, &g_pwm1_arr);
  }
  else if (pwm_num == 2)
  {
    uint32_t freq = ((uint32_t)g_pwm2_freq_h << 16) | g_pwm2_freq_l;
    PWM_Apply(&htim4_pwm, TIM_CHANNEL_1, freq, g_pwm2_duty,
              g_pwm2_ctrl & 0x01, &g_pwm2_psc, &g_pwm2_arr);
  }
}

/* --- USB Event Helpers (called from USB ISR callbacks) --- */

void USB_EnqueueCdcRx(const uint8_t *data, uint16_t len)
{
  /* CDC now uses FIFO — this is called from usb_device.c CDC_Receive_FS
   * which already calls CDC_RxPush + CDC_RxNotifyFromISR directly.
   * This stub is kept for API compatibility. */
  (void)data; (void)len;
}

void USB_EnqueueHidRx(const uint8_t *data, uint16_t len)
{
  if (hid_evt_queue == NULL) return;
  HidEvent_t evt;
  evt.len = (len > 64) ? 64 : len;
  memcpy(evt.data, data, evt.len);
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(hid_evt_queue, &evt, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* --- USB Processing Task (FIFO-based) --- */

static char     usb_line_buf[128];
static uint16_t usb_line_pos;

static void UsbTask(void *pvParameters)
{
  (void)pvParameters;
  static uint8_t tx_chunk[64];
  static uint8_t hid_resp[64];
  HidEvent_t hid_evt;

  usb_line_pos = 0;

  for (;;)
  {
    /* 1. Drain TX FIFO → USB CDC IN (non-blocking) */
    if (CDC_TxAvailable() > 0 && !CDC_TxBusy())
    {
      uint16_t n = CDC_TxPop(tx_chunk, 64);
      if (n > 0)
      {
        USBD_CDC_Transmit(&hUsbDeviceFS, tx_chunk, n);
      }
    }

    /* 2. Drain RX FIFO → parse lines → push responses to TX FIFO */
    {
      int16_t c;
      while ((c = CDC_RxPopByte()) >= 0)
      {
        if (c == '\r' || c == '\n')
        {
          if (usb_line_pos > 0)
          {
            usb_line_buf[usb_line_pos] = '\0';
            CDC_TxPush((const uint8_t *)"\r\n", 2);
            CDC_ParseLine(usb_line_buf);
            usb_line_pos = 0;
          }
        }
        else
        {
          if (usb_line_pos < sizeof(usb_line_buf) - 1)
          {
            usb_line_buf[usb_line_pos++] = (char)c;
          }
        }
      }
    }

    /* 3. Check HID queue (non-blocking) */
    if (xQueueReceive(hid_evt_queue, &hid_evt, 0) == pdTRUE)
    {
      HID_ProcessReport(hid_evt.data, hid_resp);
      USBD_HID_SendReport(&hUsbDeviceFS, hid_resp, 64);
    }

    /* 4. Sleep if nothing to do — wake on RX data or timeout */
    if (CDC_RxAvailable() == 0 && CDC_TxAvailable() == 0)
    {
      CDC_RxWait(5);
    }
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
