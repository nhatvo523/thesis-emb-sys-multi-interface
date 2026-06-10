#include "uart_task.h"
#include "modbus_slave.h"
#include <driver/uart.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "UART";

// ===== GLOBAL DEFINITIONS =====
// QueueHandle_t     xRelayQueue;   // disabled — relay TX removed
SemaphoreHandle_t xSensorMutex;
SensorData_t      gSensorData = {0.0f, 0.0f, false, {false, false, false, false}};

// ===== INIT UART2 =====
void initUart() {
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(UART_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "UART2 initialized TX=%d RX=%d", UART_TX_PIN, UART_RX_PIN);
}

// ===== PARSER: "$DATA,25.3,60.1,1,0,1,0#" =====
static bool parseDataFrame(const char* frame, float* temp, float* hum, bool* inputs) {
    if (frame[0] != '$') return false;
    const char* end = strchr(frame, '#');
    if (!end) return false;

    char buf[DATA_MAX_LEN];
    int len = (int)(end - frame - 1);
    if (len <= 0 || len >= DATA_MAX_LEN) return false;
    memcpy(buf, frame + 1, len);
    buf[len] = '\0';

    char* token = strtok(buf, ",");
    if (!token || strcmp(token, "DATA") != 0) return false;

    token = strtok(NULL, ",");
    if (!token) return false;
    *temp = strtof(token, NULL);

    token = strtok(NULL, ",");
    if (!token) return false;
    *hum = strtof(token, NULL);

    // Parse 4 inputs
    for (int i = 0; i < 4; i++) {
        token = strtok(NULL, ",");
        inputs[i] = token ? (atoi(token) != 0) : false;
    }

    return true;
}

// ===== UART RX TASK =====
void uartRxTask(void* pvParameters) {
    static uint8_t rxBuf[UART_BUF_SIZE];
    static char    frameBuf[DATA_MAX_LEN];
    static int     frameIdx = 0;
    bool           inFrame  = false;

    ESP_LOGI(TAG, "RX Task started");

    while (1) {
        int len = uart_read_bytes(UART_PORT, rxBuf,
                                  sizeof(rxBuf) - 1,
                                  pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)rxBuf[i];

            if (c == '$') {
                inFrame  = true;
                frameIdx = 0;
                frameBuf[frameIdx++] = c;
            } else if (inFrame) {
                if (frameIdx < DATA_MAX_LEN - 1) {
                    frameBuf[frameIdx++] = c;
                }
                if (c == '#') {
                    frameBuf[frameIdx] = '\0';
                    inFrame  = false;
                    frameIdx = 0;

                    float t   = 0, h = 0;
                    bool  inp[4] = {false};

                    if (parseDataFrame(frameBuf, &t, &h, inp)) {
                        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50))) {
                            gSensorData.temp  = t;
                            gSensorData.hum   = h;
                            gSensorData.valid = true;
                            for (int j = 0; j < 4; j++)
                                gSensorData.input[j] = inp[j];
                            xSemaphoreGive(xSensorMutex);
                            ESP_LOGI(TAG, "T=%.1f H=%.1f IN=%d%d%d%d",
                                     t, h, inp[0], inp[1], inp[2], inp[3]);
                        }

                        /* Mirror sensor data into Modbus holding registers:
                         *   Reg 0: Temperature × 10  (e.g. 25.3 → 253)
                         *   Reg 1: Humidity    × 10  (e.g. 60.1 → 601)
                         *   Reg 2: Digital inputs bitmask (bit0=IN1…bit3=IN4)
                         *   Reg 3–7: Reserved (0)
                         */
                        uint16_t mb_regs[MB_REG_COUNT] = {0};
                        mb_regs[0] = (uint16_t)(t * 10.0f + 0.5f);
                        mb_regs[1] = (uint16_t)(h * 10.0f + 0.5f);
                        mb_regs[2] = (uint16_t)((inp[0] ? 0x01U : 0U) |
                                                 (inp[1] ? 0x02U : 0U) |
                                                 (inp[2] ? 0x04U : 0U) |
                                                 (inp[3] ? 0x08U : 0U));
                        modbusSlave_UpdateRegs(mb_regs);
                    } else {
                        ESP_LOGW(TAG, "Bad frame: %s", frameBuf);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== UART TX TASK — disabled (relay via UART2 text protocol no longer used) =====
// void uartTxTask(void* pvParameters) {
//     RelayCmd_t cmd;
//     ESP_LOGI(TAG, "TX Task started");
//     while (1) {
//         if (xQueueReceive(xRelayQueue, &cmd, portMAX_DELAY) == pdTRUE) {
//             int len = strlen(cmd.cmd);
//             uart_write_bytes(UART_PORT, cmd.cmd, len);
//             uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
//             ESP_LOGI(TAG, "Sent: %s", cmd.cmd);
//         }
//     }
// }