#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ===== CONFIG =====
#define UART_PORT       UART_NUM_2
#define UART_TX_PIN     17
#define UART_RX_PIN     16
#define UART_BAUD       115200
#define UART_BUF_SIZE   2048

// ===== PROTOCOL =====
#define CMD_MAX_LEN     32
#define DATA_MAX_LEN    64

// Shared sensor data (protected by mutex)
typedef struct {
    float temp;
    float hum;
    bool  valid;
    bool  input[4];  // IN1..IN4
} SensorData_t;

// Command queue item — disabled (relay via UART2 text protocol no longer used)
// typedef struct {
//     char cmd[CMD_MAX_LEN];
// } RelayCmd_t;

// Extern handles
// extern QueueHandle_t     xRelayQueue;  // disabled — no relay TX queue
extern SemaphoreHandle_t xSensorMutex;
extern SensorData_t      gSensorData;

// Task prototypes
void uartRxTask(void* pvParameters);
// void uartTxTask(void* pvParameters);  // disabled — relay TX removed
void initUart();