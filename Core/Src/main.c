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
static uint8_t i2c_tx_buf[4];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void FreqCounter_Reconfigure(void);
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
  HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);

  HAL_NVIC_SetPriority(I2C1_EV_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_SetPriority(I2C1_ER_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);

  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
  HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_2);
  HAL_I2C_EnableListen_IT(&hi2c1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (HAL_GetTick() - g_last_capture_tick > FREQ_TIMEOUT_MS)
    {
      g_period_ticks = 0;
      g_frequency_hz = 0;
      g_duty_centipct = 0;
      g_pulse_ticks = 0;
    }
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

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                           uint16_t AddrMatchCode)
{
  if (hi2c->Instance != I2C1) return;

  if (TransferDirection == I2C_DIRECTION_RECEIVE)
  {
    /* Master is writing: receive reg addr + up to 2 data bytes */
    HAL_I2C_Slave_Seq_Receive_IT(hi2c, i2c_rx_buf, 3, I2C_FIRST_FRAME);
  }
  else /* I2C_DIRECTION_TRANSMIT */
  {
    /* Master is reading: respond based on selected register */
    uint32_t val = 0;
    uint8_t tx_len = 4;

    switch (i2c_reg_addr)
    {
    case REG_PERIOD: val = g_period_ticks; break;
    case REG_FREQ:   val = g_frequency_hz; break;
    case REG_DUTY:   val = g_duty_centipct; break;
    case REG_PULSE:  val = g_pulse_ticks; break;
    case REG_EDGE:
      i2c_tx_buf[0] = g_edge_config;
      HAL_I2C_Slave_Seq_Transmit_IT(hi2c, i2c_tx_buf, 1, I2C_LAST_FRAME);
      return;
    case REG_TIM_PSC:
      memcpy(i2c_tx_buf, &g_tim_psc, 2);
      HAL_I2C_Slave_Seq_Transmit_IT(hi2c, i2c_tx_buf, 2, I2C_LAST_FRAME);
      return;
    case REG_IC_PSC:
      i2c_tx_buf[0] = g_ic_psc;
      HAL_I2C_Slave_Seq_Transmit_IT(hi2c, i2c_tx_buf, 1, I2C_LAST_FRAME);
      return;
    default: break;
    }
    memcpy(i2c_tx_buf, &val, 4);
    HAL_I2C_Slave_Seq_Transmit_IT(hi2c, i2c_tx_buf, tx_len, I2C_LAST_FRAME);
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
