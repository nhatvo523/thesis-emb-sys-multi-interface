/**
 * @file    modbus_task.h
 * @brief   Public interface for the Modbus RTU master task (USART1 + DMA).
 *
 * The Modbus task runs as a FreeRTOS task and implements a Modbus RTU
 * master on USART1 at 9600 baud.  It polls MB_SLAVE_COUNT slaves
 * sequentially using function code 0x03 (Read Holding Registers).
 *
 * Only Modbus_Task_Init(), Modbus_Task_Run(), and
 * Modbus_UART_IdleCallback() are exposed here.
 */

#ifndef MODBUS_TASK_H
#define MODBUS_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Called once before osKernelStart() to enable the USART1 IDLE
 *         interrupt used for end-of-frame detection.
 *         Must be called after MX_USART1_UART_Init() and MX_DMA_Init().
 */
void Modbus_Task_Init(void);

/**
 * @brief  Modbus RTU master task entry point (osThreadFunc_t / TaskFunction_t).
 *
 * @param  argument  Unused (pass NULL).
 */
void Modbus_Task_Run(void *argument);

/**
 * @brief  Called from USART1_IRQHandler after clearing the IDLE flag.
 *         Unblocks Modbus_Task_Run() via task notification.
 */
void Modbus_UART_IdleCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_TASK_H */
