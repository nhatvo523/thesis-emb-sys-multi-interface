/**
 * @file    uart_esp32_task.h
 * @brief   Public interface for the UART-ESP32 communication task (USART1 + DMA).
 *
 * The task runs on USART1 at 115200 baud and exchanges state/command frames
 * with an ESP32 co-processor.
 *
 * TX frame (STM32 → ESP32, sent every UART_ESP32_SEND_PERIOD_MS):
 *   [0xAA][relay_state][di_state][checksum][0x55]   — 5 bytes
 *   checksum = relay_state ^ di_state ^ 0xAA
 *
 * RX frame (ESP32 → STM32, relay control command):
 *   [0xBB][relay_mask][checksum][0x55]               — 4 bytes
 *   checksum = relay_mask ^ 0xBB
 *   relay_mask: bit N = 1 → energise relay N (same layout as g_appData.relay_state)
 *
 * On receiving a valid RX frame the task writes mb_coil[0..3] under g_dataMutex
 * and sets EVT_RELAY_CMD so IO_Task_Run() latches the new relay state to GPIO.
 */

#ifndef UART_ESP32_TASK_H
#define UART_ESP32_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Periodic TX interval (ms). */
#define UART_ESP32_SEND_PERIOD_MS   100U

/**
 * @brief  Called once before osKernelStart() to enable the USART1 IDLE
 *         interrupt used for end-of-frame detection.
 *         Must be called after MX_USART1_UART_Init() and MX_DMA_Init().
 */
void UART_ESP32_Task_Init(void);

/**
 * @brief  UART-ESP32 task entry point (osThreadFunc_t / TaskFunction_t).
 *
 * @param  argument  Unused (pass NULL).
 */
void UART_ESP32_Task_Run(void *argument);

/**
 * @brief  Called from USART1_IRQHandler after clearing the IDLE flag.
 *         Unblocks UART_ESP32_Task_Run() via task notification.
 */
void UART_ESP32_IdleCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_ESP32_TASK_H */
