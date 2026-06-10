/**
 * @file    modbus_slave.h
 * @brief   Modbus RTU slave — UART1, 9600 8N1, RS-485 half-duplex.
 *
 * Responds to FC 0x03 (Read Holding Registers) and FC 0x01 exception
 * (Illegal Function) from the STM32 Modbus master task.
 *
 * This project currently builds a single Modbus slave at address 1.
 *
 * Register map (MB_REG_COUNT = 8, matches STM32 MB_SLAVE_REG_COUNT):
 * ┌─────┬──────────────────────────────────────────────────────────────┐
 * │ Reg │ Content                                                      │
 * ├─────┼──────────────────────────────────────────────────────────────┤
 * │  0  │ Temperature × 10  (25.3 °C  →  253)                         │
 * │  1  │ Humidity    × 10  (60.1 %   →  601)                         │
 * │  2  │ Digital inputs bitmask  (bit0=IN1 … bit3=IN4)               │
 * │ 3–7 │ Reserved, always 0                                          │
 * └─────┴──────────────────────────────────────────────────────────────┘
 *
 * Hardware wiring:
 *   UART1  TX (GPIO17 / TX2) → RS-485 module TX input
 *   UART1  RX (GPIO16 / RX2) ← RS-485 module RX output
 *   3V3 / GND          ↔  RS-485 module power
 *   Direction control is handled internally by the RS-485 module.
 */

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <driver/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== HARDWARE CONFIG ==================================================== */

/** ESP32 UART peripheral used for Modbus RTU. */
#define MB_UART_PORT    UART_NUM_1

/** GPIO connected to the RS-485 transceiver DI (driver input / TX). */
#define MB_TX_PIN       17

/** GPIO connected to the RS-485 transceiver RO (receiver output / RX). */
#define MB_RX_PIN       16

/* ===== MODBUS PARAMETERS ================================================= */

/** Baud rate — must match STM32 USART1 (9600). */
#define MB_BAUD_RATE    9600U

/**
 * Modbus slave address for this ESP32 node.
 */
#define MB_SLAVE_ADDR   1U

/**
 * Number of holding registers exposed.
 * MUST equal MB_SLAVE_REG_COUNT defined in the STM32 app_data.h (= 8).
 */
#define MB_REG_COUNT    8U

/** Maximum raw Modbus frame size. */
#define MB_FRAME_MAX    256U

static_assert(MB_FRAME_MAX >= 8, "MB_FRAME_MAX must be at least 8 bytes");
static_assert(MB_REG_COUNT > 0,  "MB_REG_COUNT must be non-zero");
static_assert(MB_SLAVE_ADDR >= 1U && MB_SLAVE_ADDR <= 247U, "MB_SLAVE_ADDR must be 1–247");
static_assert(MB_REG_COUNT <= 125U, "MB_REG_COUNT exceeds Modbus FC03 limit of 125");

/* ===== TASK CONFIG ======================================================== */

#define MB_TASK_STACK       4096
#define MB_TASK_PRIORITY    5

/* ===== PUBLIC API ========================================================= */

/**
 * @brief  Initialise the Modbus slave (UART1, register mutex).
 *         Must be called once before creating modbusSlave_Task().
 */
void modbusSlave_Init(void);

/**
 * @brief  Atomically replace the entire holding-register table.
 *         Thread-safe; may be called from any task.
 *
 * @param  regs  Source array of exactly MB_REG_COUNT uint16_t values.
 */
void modbusSlave_UpdateRegs(const uint16_t regs[MB_REG_COUNT]);

/**
 * @brief  Modbus slave FreeRTOS task.
 *         Listens on MB_UART_PORT, validates requests, and responds.
 *
 * @param  pvParameters  Unused (pass NULL).
 */
void modbusSlave_Task(void* pvParameters);

#ifdef __cplusplus
}
#endif
