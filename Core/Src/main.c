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
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TIM2_BASE_CLOCK_HZ  100000000UL
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

/* Flash config storage - Sector 7 (last 128KB sector of STM32F411CE) */
#define CONFIG_FLASH_SECTOR   FLASH_SECTOR_7
#define CONFIG_FLASH_ADDR     0x08060000UL
#define CONFIG_MAGIC          0xDEADBEEFUL
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

static uint8_t  g_edge_config = EDGE_RISING;
static uint16_t g_tim_psc = 0;
static uint8_t  g_ic_psc = 0;           /* 0=DIV1, 1=DIV2, 2=DIV4, 3=DIV8 */

/* I2C register protocol state */
static uint8_t i2c_reg_addr = 0xFF;
static uint8_t i2c_rx_buf[3];           /* reg addr + up to 2 data bytes */
static uint8_t i2c_tx_buf[41];         /* full register map for burst reads (0x00-0x28) */

/* LED parameters (written by I2C, read by LedTask) */
static uint16_t g_led_period_ms = LED_DEFAULT_PERIOD;
static uint8_t  g_led_duty_pct = LED_DEFAULT_DUTY;
static uint16_t g_led_g_period_ms = LED_DEFAULT_PERIOD;
static uint8_t  g_led_g_duty_pct = LED_DEFAULT_DUTY;
static uint16_t g_led_r_period_ms = LED_DEFAULT_PERIOD;
static uint8_t  g_led_r_duty_pct = LED_DEFAULT_DUTY;

/* FreeRTOS */
#define LED_TASK_STACK_SIZE      256  /* words = 1024 bytes */
#define MONITOR_TASK_STACK_SIZE  256  /* words = 1024 bytes */
static TaskHandle_t hLedTask = NULL;
static TaskHandle_t hMonitorTask = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void FreqCounter_Reconfigure(void);
static void LED_Init(void);
static void LED_G_Init(void);
static void LED_R_Init(void);
static void Config_Load(void);
static void Config_Save(void);
static void LedTask(void *pvParameters);
static void MonitorTask(void *pvParameters);
static uint8_t I2C_BuildTxBuffer(uint8_t start_reg);
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
  /* USER CODE BEGIN 2 */
  LED_Init();
  LED_G_Init();
  LED_R_Init();
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

  /* Create FreeRTOS tasks */
  xTaskCreate(LedTask, "LED", LED_TASK_STACK_SIZE, NULL, 1, &hLedTask);
  xTaskCreate(MonitorTask, "MON", MONITOR_TASK_STACK_SIZE, NULL, 1, &hMonitorTask);

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 100;
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
}

static void Config_Save(void)
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

static void FreqCounter_Reconfigure(void)
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

#define REG_MAP_END  0x29  /* one past last register byte (LED_R_DUTY at 0x28) */

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
