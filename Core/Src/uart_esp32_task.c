/**
 * @file    uart_esp32_task.c
 * @brief   UART communication task with ESP32 co-processor — USART1, 115200 8N1.
 *
 * Protocol
 * --------
 * TX (STM32 → ESP32, every UART_ESP32_SEND_PERIOD_MS):
 *   Byte 0  : 0xAA  — Start-of-Frame
 *   Byte 1  : relay_state  (bit N = relay N energised)
 *   Byte 2  : di_state     (bit N = digital input N active)
 *   Byte 3  : relay_state ^ di_state ^ 0xAA  — XOR checksum
 *   Byte 4  : 0x55  — End-of-Frame
 *
 * RX (ESP32 → STM32, relay control command, may arrive at any time):
 *   Byte 0  : 0xBB  — Start-of-Frame
 *   Byte 1  : relay_mask  (bit N = 1 → energise relay N)
 *   Byte 2  : relay_mask ^ 0xBB  — XOR checksum
 *   Byte 3  : 0x55  — End-of-Frame
 *
 * Receive mechanism
 * -----------------
 * DMA receive is re-armed after each TX.  The USART1 IDLE interrupt fires
 * when the ESP32 frame has been fully clocked in and the line goes idle.
 * UART_ESP32_IdleCallback() (called from USART1_IRQHandler) sends a task
 * notification to unblock ulTaskNotifyTake().
 *
 * On a valid RX frame the task writes g_appData.mb_coil[0..3] under
 * g_dataMutex and sets EVT_RELAY_CMD so IO_Task_Run() applies the new
 * relay mask to GPIO without any extra delay.
 *
 * Timing (per cycle, UART_ESP32_SEND_PERIOD_MS = 100 ms)
 * -------------------------------------------------------
 *   TX blocking transmit  : ~0.5 ms at 115200 baud (5 bytes)
 *   RX wait (timeout)     : ESP_RX_WAIT_MS = 80 ms
 *   Remainder delay       : 100 − 80 = 20 ms
 *   Total                 : ≈ 100 ms
 */

#include "uart_esp32_task.h"
#include "app_data.h"           /* g_appData, g_dataMutex, EVT_RELAY_CMD     */
#include "main.h"               /* SRAM_DMA macro                            */
#include "cmsis_os.h"           /* osMutexAcquire/Release, osDelay           */
#include "FreeRTOS.h"           /* TaskHandle_t, ulTaskNotifyTake            */
#include "task.h"               /* xTaskGetCurrentTaskHandle                 */
#include "event_groups.h"       /* xEventGroupSetBits                        */
#include "stm32f4xx_hal.h"      /* UART, DMA HAL                             */

/* ===========================================================================
 * External HAL handles
 * ========================================================================= */
extern UART_HandleTypeDef huart1;

/* ===========================================================================
 * Private constants
 * ========================================================================= */

/** TX frame layout */
#define ESP_TX_SOF          0xAAU
#define ESP_TX_EOF          0x55U
#define ESP_TX_LEN          5U      /* SOF + relay + di + chk + EOF */

/** RX frame layout */
#define ESP_RX_SOF          0xBBU
#define ESP_RX_EOF          0x55U
#define ESP_RX_LEN          4U      /* SOF + relay_cmd + chk + EOF */

/**
 * Time (ms) to wait for an incoming ESP32 frame after sending the TX frame.
 * Must be less than UART_ESP32_SEND_PERIOD_MS.
 */
#define ESP_RX_WAIT_MS      80U

/** Remainder delay to pad the cycle to UART_ESP32_SEND_PERIOD_MS. */
#define ESP_CYCLE_PAD_MS    (UART_ESP32_SEND_PERIOD_MS - ESP_RX_WAIT_MS)

/** DMA RX buffer size — must be ≥ ESP_RX_LEN; small margin for noise bytes. */
#define ESP_RX_BUF_LEN      8U

/* ===========================================================================
 * Private data
 * ========================================================================= */

/**
 * DMA receive buffer.
 * SRAM_DMA = default SRAM (accessible by DMA2 used for USART1).
 * NOT in CCMRAM — DMA cannot access CCMRAM on STM32F4.
 */
SRAM_DMA static uint8_t s_rx_buf[ESP_RX_BUF_LEN];

/** Handle of this task — set in Task_Run, used by IdleCallback ISR. */
static TaskHandle_t s_task_handle = NULL;

/* ===========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Scan a raw DMA buffer for a valid ESP32 RX frame.
 *
 * Searches for the 0xBB SOF byte, then validates checksum and EOF.
 *
 * @param  buf   Pointer to received bytes.
 * @param  len   Number of valid bytes in buf.
 * @param[out] relay_out  Relay mask extracted from the frame (0 if not found).
 * @return  1 if a valid frame was found, 0 otherwise.
 */
static uint8_t find_rx_frame(const uint8_t *buf, uint16_t len, uint8_t *relay_out)
{
    for (uint16_t i = 0U; (i + ESP_RX_LEN) <= len; i++)
    {
        if (buf[i] != ESP_RX_SOF) { continue; }

        uint8_t relay_cmd = buf[i + 1U];
        uint8_t chk       = buf[i + 2U];
        uint8_t eof       = buf[i + 3U];

        if (chk != (relay_cmd ^ ESP_RX_SOF)) { continue; }
        if (eof != ESP_RX_EOF)               { continue; }

        *relay_out = relay_cmd;
        return 1U;
    }
    return 0U;
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void UART_ESP32_Task_Init(void)
{
    /* Enable USART1 IDLE interrupt so UART_ESP32_IdleCallback() fires when
     * an incoming frame from ESP32 has been fully received and the line
     * goes idle.  Must be called after MX_USART1_UART_Init(). */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

void UART_ESP32_IdleCallback(void)
{
    /* Called from USART1_IRQHandler (ISR context). */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(s_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void UART_ESP32_Task_Run(void *argument)
{
    (void)argument;

    s_task_handle = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        /* -----------------------------------------------------------------
         * 1. Read current relay and DI state under mutex (non-blocking copy).
         * ---------------------------------------------------------------- */
        uint8_t relay_s = 0U;
        uint8_t di_s    = 0U;

        if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
        {
            relay_s = g_appData.relay_state;
            di_s    = g_appData.di_state;
            osMutexRelease(g_dataMutex);
        }

        /* -----------------------------------------------------------------
         * 2. Build and send TX frame (blocking — completes in < 1 ms).
         * ---------------------------------------------------------------- */
        uint8_t tx[ESP_TX_LEN];
        tx[0] = ESP_TX_SOF;
        tx[1] = relay_s;
        tx[2] = di_s;
        tx[3] = relay_s ^ di_s ^ ESP_TX_SOF;   /* XOR checksum */
        tx[4] = ESP_TX_EOF;
        HAL_UART_Transmit(&huart1, tx, ESP_TX_LEN, 10U);

        /* -----------------------------------------------------------------
         * 3. Discard any stale notifications accumulated during TX, then
         *    arm DMA RX for the incoming ESP32 response/command.
         * ---------------------------------------------------------------- */
        ulTaskNotifyTake(pdTRUE, 0U);           /* clear without blocking  */

        HAL_UART_AbortReceive(&huart1);
        HAL_UART_Receive_DMA(&huart1, s_rx_buf, ESP_RX_BUF_LEN);

        /* -----------------------------------------------------------------
         * 4. Block until IDLE notification (ESP32 frame arrived) or timeout.
         * ---------------------------------------------------------------- */
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ESP_RX_WAIT_MS));

        if (notified > 0U)
        {
            /* Stop DMA and measure how many bytes were received. */
            HAL_UART_AbortReceive(&huart1);
            uint16_t rx_bytes = (uint16_t)(ESP_RX_BUF_LEN
                                - __HAL_DMA_GET_COUNTER(huart1.hdmarx));

            /* Parse frame and apply relay command if valid. */
            uint8_t relay_cmd = 0U;
            if (find_rx_frame(s_rx_buf, rx_bytes, &relay_cmd))
            {
                /* Write new coil state under mutex. */
                if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
                {
                    for (uint8_t b = 0U; b < 4U; b++)
                    {
                        g_appData.mb_coil[b] = (relay_cmd >> b) & 0x01U;
                    }
                    osMutexRelease(g_dataMutex);
                }
                /* Wake IO task to latch new relay state onto GPIO. */
                xEventGroupSetBits(g_systemEvents, EVT_RELAY_CMD);
            }
        }
        else
        {
            /* Timeout — no frame received; stop DMA cleanly. */
            HAL_UART_AbortReceive(&huart1);
        }

        /* -----------------------------------------------------------------
         * 5. Pad the cycle to UART_ESP32_SEND_PERIOD_MS.
         * ---------------------------------------------------------------- */
        osDelay(ESP_CYCLE_PAD_MS);
    }
}
