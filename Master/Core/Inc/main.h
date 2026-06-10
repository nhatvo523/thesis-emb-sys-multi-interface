/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

typedef enum
{
  SYSTEM_EXCEPTION_NONE = 0,
  SYSTEM_EXCEPTION_NMI,
  SYSTEM_EXCEPTION_HARDFAULT,
  SYSTEM_EXCEPTION_MEMMANAGE,
  SYSTEM_EXCEPTION_BUSFAULT,
  SYSTEM_EXCEPTION_USAGEFAULT,
  SYSTEM_EXCEPTION_ERROR_HANDLER
} SystemExceptionCode_t;

typedef struct
{
  uint32_t signature;
  uint32_t code;
  uint32_t ipsr;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t mmfar;
  uint32_t bfar;
  uint32_t msp;
  uint32_t psp;
  uint32_t tick;
} SystemExceptionInfo_t;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
extern IWDG_HandleTypeDef hiwdg; /* Independent Watchdog handle */
extern volatile SystemExceptionInfo_t g_exceptionInfo;

void System_ExceptionTrap(SystemExceptionCode_t code);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

/* ---------------------------------------------------------------------------
 * Relay output pin definitions
 * Relay logic: GPIO HIGH = relay energised.
 * -------------------------------------------------------------------------*/
#define RELAY1_PORT  GPIOB
#define RELAY1_PIN   GPIO_PIN_8

#define RELAY2_PORT  GPIOB
#define RELAY2_PIN   GPIO_PIN_9

#define RELAY3_PORT  GPIOE
#define RELAY3_PIN   GPIO_PIN_0

#define RELAY4_PORT  GPIOE
#define RELAY4_PIN   GPIO_PIN_1

/* ---------------------------------------------------------------------------
 * Ethernet PHY control pin definitions (LAN8720AI-CP-TR, RMII)
 * -------------------------------------------------------------------------*/

/* PA8 — ETH_VDD power enable via N-ch/P-ch MOSFET pair (Q401/Q400).
 * Logic INVERTED: HIGH = ETH_VDD ON, LOW = ETH_VDD OFF.
 * WARNING: PA8 is MCO1-capable — do NOT configure RCC MCO1 output. */
#define ETH_PWR_CTRL_PORT  GPIOA
#define ETH_PWR_CTRL_PIN   GPIO_PIN_8

/* PB14 — OE of Y400 50 MHz CMOS oscillator → XTAL1 of LAN8720.
 * HIGH = clock running, LOW = output Hi-Z (oscillator silent). */
#define ETH_CR_EN_PORT     GPIOB
#define ETH_CR_EN_PIN      GPIO_PIN_14

/* PB15 — active-low reset of LAN8720 (RST pin 15).
 * LOW = PHY in reset, HIGH = PHY running.
 * Pull-up R403 (4.7 kΩ) to ETH_VDD (not 3V3) on RST line. */
#define ETH_RST_PORT       GPIOB
#define ETH_RST_PIN        GPIO_PIN_15

/* ---------------------------------------------------------------------------
 * Memory placement macros
 *
 *  CCMRAM_BSS  — uninitialised (zero) variable in CCMRAM
 *                CPU-only: task stacks, calc buffers, lookup tables
 *                ❌ NOT for DMA targets (ETH, UART DMA, USB, ADC DMA)
 *
 *  SRAM_DMA    — explicit SRAM placement (default for all globals, but
 *                the macro documents intent: this buffer IS a DMA target)
 *
 * Examples:
 *   CCMRAM_BSS static uint8_t  modbus_scratch[256];
 *   SRAM_DMA   static uint8_t  uart_rx_dma[64];
 * -------------------------------------------------------------------------*/
#define CCMRAM_BSS  __attribute__((section(".ccmram")))
#define SRAM_DMA    /* default SRAM – no attribute needed */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
