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
#include "cmsis_os.h"
#include "lwip.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_data.h"
#include "ethernet_task.h"
#include "io_task.h"
#include "usb_task.h"
#include "modbus_task.h"
/* #include "uart_esp32_task.h" */   /* UART-ESP32 task disabled — USART1 used by Modbus task */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

/* Definitions for Task_Ethernet */
osThreadId_t Task_EthernetHandle;
const osThreadAttr_t Task_Ethernet_attributes = {
  .name = "Task_Ethernet",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Modbus */
osThreadId_t Task_ModbusHandle;
const osThreadAttr_t Task_Modbus_attributes = {
  .name = "Task_Modbus",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_IO */
osThreadId_t Task_IOHandle;
const osThreadAttr_t Task_IO_attributes = {
  .name = "Task_IO",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_USB */
osThreadId_t Task_USBHandle;
const osThreadAttr_t Task_USB_attributes = {
  .name = "Task_USB",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* USER CODE BEGIN PV */
IWDG_HandleTypeDef hiwdg;
volatile SystemExceptionInfo_t g_exceptionInfo = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
void StartTask01(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);

/* USER CODE BEGIN PFP */

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
  MX_DMA_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  Modbus_Task_Init();
  /* UART_ESP32_Task_Init(); */   /* Disabled — USART1 is now used by Modbus task */
  IO_Task_Init();
  USB_Task_Init();
  Ethernet_Task_Init();

  /* Power-up the Ethernet PHY (LAN8720AI) before the RTOS scheduler starts.
   * HAL_Delay() is used here (SysTick-based, works bare-metal pre-scheduler).
   * Call order:
   *   1. SystemClock_Config()  — SysTick running at 168 MHz
  *   2. MX_GPIO_Init()        — PA8/PB14/PB15 forced safe-low
  *   3. ETH_PowerUp()         — sequenced rail/clock/reset release
   *   4. osKernelStart()       — RTOS starts; Ethernet_Task_Run() calls
   *                              MX_LWIP_Init() → HAL_ETH_Init() internally */
  ETH_PowerUp();

  /* Initialise USB device stack here (before RTOS) so Windows enumerates
   * the CDC port immediately at boot, independent of task scheduling.
   * USB interrupts are handled in ISR context — no task needed for enumeration. */
  MX_USB_DEVICE_Init();

  /* Start the Independent Watchdog (IWDG).
   * Clock  : LSI ≈ 32 kHz
   * Prescaler /32  → tick = 1 ms
   * Reload  3999  → timeout = 4000 ms
   * The lowest-priority task (USB_Task, osPriorityLow) kicks the
   * watchdog every ≤ 1000 ms.  If any task deadlocks and starves USB_Task
   * for > 4 s the MCU resets safely via ETH_PowerDown() in fault handlers.
   * During debug: __HAL_DBGMCU_FREEZE_IWDG() pauses the counter on breakpoints. */
#ifdef DEBUG
  /* In debug builds freeze the IWDG counter while the core is halted on a
   * breakpoint; the watchdog is NOT started so it never fires during debug. */
  __HAL_DBGMCU_FREEZE_IWDG();
#else
  hiwdg.Instance       = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Reload    = 3999U;   /* 4000 ms — covers USB enumeration delay */
  HAL_IWDG_Init(&hiwdg);
#endif

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  App_Data_Init();
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_Ethernet */
  Task_EthernetHandle = osThreadNew(StartTask01, NULL, &Task_Ethernet_attributes);

  /* creation of Task_Modbus */
  Task_ModbusHandle = osThreadNew(StartTask02, NULL, &Task_Modbus_attributes);

  /* creation of Task_IO */
  Task_IOHandle = osThreadNew(StartTask03, NULL, &Task_IO_attributes);

  /* creation of Task_USB */
  Task_USBHandle = osThreadNew(StartTask04, NULL, &Task_USB_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  /* DMA2_Stream7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pins : PE14 PE15 */
  GPIO_InitStruct.Pin = GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PB4 PB5 PB6 PB7 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* Configure relay outputs on GPIOB: PB8 (Relay1), PB9 (Relay2) */
  GPIO_InitStruct.Pin   = RELAY1_PIN | RELAY2_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RELAY1_PORT, &GPIO_InitStruct);
  /* Relays off at startup */
  HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN | RELAY2_PIN, GPIO_PIN_RESET);

  /* Configure relay outputs on GPIOE: PE0 (Relay3), PE1 (Relay4) */
  GPIO_InitStruct.Pin   = RELAY3_PIN | RELAY4_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RELAY3_PORT, &GPIO_InitStruct);
  /* Relays off at startup */
  HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN | RELAY4_PIN, GPIO_PIN_RESET);

  /* -------------------------------------------------------------------------
   * Ethernet PHY control outputs — PA8, PB14, PB15
   * GPIOA and GPIOB clocks are already enabled above by CubeMX.
   * ------------------------------------------------------------------------- */

  /* PA8 — ETH_PWR_CTRL: push-pull output, no pull, low speed.
   * Initialise LOW so PHY power stays off until ETH_PowerUp() performs the
   * full rail/clock/reset sequence. (HIGH = ETH_VDD ON due to inverted
   * MOSFET topology) */
  GPIO_InitStruct.Pin   = ETH_PWR_CTRL_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ETH_PWR_CTRL_PORT, &GPIO_InitStruct);
  HAL_GPIO_WritePin(ETH_PWR_CTRL_PORT, ETH_PWR_CTRL_PIN, GPIO_PIN_RESET);

  /* PB14 — ETH_CR_EN: push-pull output, no pull, low speed.
   * Initialise LOW: keeps 50 MHz oscillator output Hi-Z until power-up sequence. */
  GPIO_InitStruct.Pin   = ETH_CR_EN_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ETH_CR_EN_PORT, &GPIO_InitStruct);
  HAL_GPIO_WritePin(ETH_CR_EN_PORT, ETH_CR_EN_PIN, GPIO_PIN_RESET);

  /* PB15 — ETH_RST (active-low): push-pull output, no pull, low speed.
   * Initialise LOW: holds LAN8720 in reset until power-up sequence. */
  GPIO_InitStruct.Pin   = ETH_RST_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ETH_RST_PORT, &GPIO_InitStruct);
  HAL_GPIO_WritePin(ETH_RST_PORT, ETH_RST_PIN, GPIO_PIN_RESET);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void System_ExceptionTrap(SystemExceptionCode_t code)
{
  __disable_irq();

  g_exceptionInfo.signature = 0x45584350UL; /* 'EXCP' */
  g_exceptionInfo.code = (uint32_t)code;
  g_exceptionInfo.ipsr = __get_IPSR();
  g_exceptionInfo.cfsr = SCB->CFSR;
  g_exceptionInfo.hfsr = SCB->HFSR;
  g_exceptionInfo.dfsr = SCB->DFSR;
  g_exceptionInfo.afsr = SCB->AFSR;
  g_exceptionInfo.mmfar = SCB->MMFAR;
  g_exceptionInfo.bfar = SCB->BFAR;
  g_exceptionInfo.msp = __get_MSP();
  g_exceptionInfo.psp = __get_PSP();
  g_exceptionInfo.tick = HAL_GetTick();

#ifndef DEBUG
  ETH_PowerDown();
#endif

  while (1)
  {
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartTask01 */
/**
  * @brief  Function implementing the Task_Ethernet thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTask01 */
void StartTask01(void *argument)
{
  /* USER CODE BEGIN 5 */
  Ethernet_Task_Run(argument);
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the Task_Modbus thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  Modbus_Task_Run(argument);
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
* @brief Function implementing the Task_IO thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  IO_Task_Run(argument);
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
* @brief Function implementing the Task_USB thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask04 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
  USB_Task_Run(argument);
  /* USER CODE END StartTask04 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  System_ExceptionTrap(SYSTEM_EXCEPTION_ERROR_HANDLER);
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
