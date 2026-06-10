/**
 * @file    modbus_slave.cpp
 * @brief   Modbus RTU slave implementation — ESP32, UART1, 9600 8N1.
 *
 * Supports:
 *   FC 0x03  Read Holding Registers (the only function code the STM32
 *            master sends).
 *   All other FCs → exception response 0x01 (Illegal Function).
 *   Any request with bad CRC → silently ignored (Modbus RTU spec §2.5.1).
 *   Register address out of range → exception 0x02 (Illegal Data Address).
 *
 * End-of-frame detection
 * ----------------------
 * Modbus RTU mandates 3.5 character-times of silence between frames.
 * At 9600 baud that is ≈ 3.65 ms.  The task reads the first byte with a
 * long idle timeout (100 ms), then loops reading with a 5 ms timeout;
 * the first 5 ms gap signals end-of-frame.
 *
 * RS-485 half-duplex
 * ------------------
 * The attached RS-485 module auto-controls transmit/receive direction,
 * so the ESP32 does not drive a DE/RE GPIO.
 */

#include "modbus_slave.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/task.h"

static const char* TAG = "MB_SLAVE";

/* ===========================================================================
 * Private state
 * ========================================================================= */

static uint16_t          s_regs[MB_REG_COUNT];
static SemaphoreHandle_t s_reg_mutex = NULL;

/* ===========================================================================
 * CRC-16 (Modbus polynomial 0xA001, identical to STM32 implementation)
 * ========================================================================= */

static uint16_t crc16(const uint8_t* buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)buf[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x0001U) ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U);
        }
    }
    return crc;
}

/* ===========================================================================
 * Private: transmit a response frame
 * ========================================================================= */

/**
 * @brief  Send @p len bytes and wait for TX complete.
 *         Also flushes the RX input buffer afterwards to discard any
 *         transceiver echo or line noise captured during TX.
 */
static void send_response(const uint8_t* data, int len)
{
    uart_write_bytes(MB_UART_PORT, (const char*)data, len);
    /* Dynamic timeout: len bytes × 10 bits / baud + 10 ms margin.
     * At 9600 baud, 21-byte response ≈ 21.9 ms → 20 ms static was too short. */
    TickType_t tx_timeout = pdMS_TO_TICKS(((uint32_t)len * 10U * 1000U) / MB_BAUD_RATE + 10U);
    uart_wait_tx_done(MB_UART_PORT, tx_timeout);
    /* Discard any bytes that crept into the RX FIFO during transmission
     * (electrical reflections, transceiver propagation, etc.). */
    uart_flush_input(MB_UART_PORT);
}

/* ===========================================================================
 * Private: send Modbus exception response
 * ========================================================================= */

/**
 * @brief  Build and transmit an exception response.
 *
 * @param  fc        Original function code from the request.
 * @param  exc_code  Modbus exception code (0x01 Illegal Function,
 *                   0x02 Illegal Data Address, etc.).
 */
static void send_exception(uint8_t fc, uint8_t exc_code)
{
    uint8_t  exc[5];
    exc[0] = (uint8_t)MB_SLAVE_ADDR;
    exc[1] = fc | 0x80U;   /* error FC = original FC + 0x80 */
    exc[2] = exc_code;
    uint16_t crc = crc16(exc, 3U);
    exc[3] = (uint8_t)(crc & 0xFFU);
    exc[4] = (uint8_t)(crc >> 8U);
    send_response(exc, 5);
    ESP_LOGW(TAG, "Exception: FC=0x%02X code=0x%02X", fc, exc_code);
}

/* ===========================================================================
 * Private: handle FC 0x03 — Read Holding Registers
 * ========================================================================= */

/**
 * @brief  Validate the register range, read s_regs[] under mutex, and
 *         transmit the FC 0x03 response.
 *
 * Request layout (8 bytes, already CRC-validated by caller):
 *   [addr][0x03][start_hi][start_lo][count_hi][count_lo][crc_lo][crc_hi]
 *
 * Response layout:
 *   [addr][0x03][byte_count][reg0_hi][reg0_lo]…[crc_lo][crc_hi]
 */
static void handle_fc03(const uint8_t* req)
{
    const uint8_t fc = req[1]; /* use actual FC from request — never hardcode */
    uint16_t start = ((uint16_t)req[2] << 8U) | (uint16_t)req[3];
    uint16_t count = ((uint16_t)req[4] << 8U) | (uint16_t)req[5];

    /* Validate address range.
     * Cast to uint32_t before addition to prevent uint16_t wrap-around:
     * e.g. start=0xFFFE + count=2 would overflow to 0 on uint16_t. */
    if ((count == 0U) || ((uint32_t)start + (uint32_t)count > (uint32_t)MB_REG_COUNT)) {
        send_exception(fc, 0x02U); /* 0x02 = Illegal Data Address */
        return;
    }

    /* Response buffer: addr(1) + fc(1) + byte_count(1) + data(count*2) + crc(2) */
    uint8_t resp[3U + MB_REG_COUNT * 2U + 2U];
    uint16_t byte_count = count * 2U;

    resp[0] = (uint8_t)MB_SLAVE_ADDR;
    resp[1] = 0x03U;
    resp[2] = (uint8_t)byte_count;

    /* Copy registers under mutex — brief critical section */
    if (xSemaphoreTake(s_reg_mutex, pdMS_TO_TICKS(10U)) == pdTRUE) {
        for (uint16_t i = 0U; i < count; i++) {
            resp[3U + i * 2U]      = (uint8_t)(s_regs[start + i] >> 8U);
            resp[3U + i * 2U + 1U] = (uint8_t)(s_regs[start + i] & 0xFFU);
        }
        xSemaphoreGive(s_reg_mutex);
    } else {
        /* Mutex timeout — send zeros rather than potentially corrupt data */
        memset(&resp[3U], 0U, byte_count);
        ESP_LOGW(TAG, "FC03 mutex timeout — responded with zeros");
    }

    uint16_t resp_len = 3U + byte_count;
    uint16_t crc      = crc16(resp, resp_len);
    resp[resp_len]     = (uint8_t)(crc & 0xFFU);
    resp[resp_len + 1U] = (uint8_t)(crc >> 8U);

    send_response(resp, (int)(resp_len + 2U));

    ESP_LOGD(TAG, "FC03 start=%u count=%u OK", start, count);
}

/* ===========================================================================
 * Public: initialise
 * ========================================================================= */

void modbusSlave_Init(void)
{
    /* Register store & mutex */
    memset(s_regs, 0U, sizeof(s_regs));
    s_reg_mutex = xSemaphoreCreateMutex();
    configASSERT(s_reg_mutex != NULL);

    /* UART1 */
    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate  = (int)MB_BAUD_RATE;
    uart_cfg.data_bits  = UART_DATA_8_BITS;
    uart_cfg.parity     = UART_PARITY_DISABLE;
    uart_cfg.stop_bits  = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_APB;

    uart_driver_install(MB_UART_PORT,
                        (int)MB_FRAME_MAX * 4,
                        (int)MB_FRAME_MAX * 4,
                        0, NULL, 0);
    uart_param_config(MB_UART_PORT, &uart_cfg);
    uart_set_pin(MB_UART_PORT,
                 MB_TX_PIN, MB_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    ESP_LOGI(TAG,
             "Modbus slave ready: addr=%u  UART%d  TX=GPIO%d  RX=GPIO%d  %u baud",
             MB_SLAVE_ADDR, MB_UART_PORT,
             MB_TX_PIN, MB_RX_PIN,
             MB_BAUD_RATE);
}

/* ===========================================================================
 * Public: update holding registers
 * ========================================================================= */

void modbusSlave_UpdateRegs(const uint16_t regs[MB_REG_COUNT])
{
    if (s_reg_mutex == NULL) return;
    if (xSemaphoreTake(s_reg_mutex, pdMS_TO_TICKS(50U)) == pdTRUE) {
        memcpy(s_regs, regs, sizeof(s_regs));
        xSemaphoreGive(s_reg_mutex);
    }
}

/* ===========================================================================
 * Public: task
 * ========================================================================= */

void modbusSlave_Task(void* pvParameters)
{
    (void)pvParameters;

    /* NOT reentrant — this task must run as a single instance only.
     * static placement avoids consuming task stack for a 256-byte buffer. */
    static uint8_t rx_buf[MB_FRAME_MAX];

    /*
     * At 9600 baud one character = 10 bits ≈ 1.04 ms.
     * Modbus RTU end-of-frame silence = 3.5 chars ≈ 3.65 ms.
     * Use 5 ms as the inter-character timeout — generous enough to
     * absorb jitter while shorter than any expected inter-frame gap.
     */
    const TickType_t inter_byte_timeout = pdMS_TO_TICKS(5U);

    ESP_LOGI(TAG, "Task running (slave addr=%u)", MB_SLAVE_ADDR);

    for (;;) {
        /* ---------------------------------------------------------------
         * Wait for the first byte of a new request frame.
         * 100 ms timeout → just yields CPU; no busy-loop.
         * --------------------------------------------------------------- */
        int total = uart_read_bytes(MB_UART_PORT, rx_buf, 1,
                                    pdMS_TO_TICKS(100U));
        if (total <= 0) {
            continue;
        }

        /* ---------------------------------------------------------------
         * Collect the rest of the frame.
         * Loop until 5 ms of silence (end-of-frame) or buffer full.
         * --------------------------------------------------------------- */
        while (total < (int)MB_FRAME_MAX) {
            int n = uart_read_bytes(MB_UART_PORT,
                                    rx_buf + total,
                                    (int)MB_FRAME_MAX - total,
                                    inter_byte_timeout);
            if (n <= 0) break; /* silence ≥ 5 ms → frame complete */
            total += n;
        }

        /* ---------------------------------------------------------------
         * A valid FC 0x03 request is exactly 8 bytes.
         * Drop anything shorter before attempting address/CRC checks.
         * --------------------------------------------------------------- */
        if (total < 8) {
            if (total > 0)
                ESP_LOGW(TAG, "Short/fragment frame (%d bytes) — dropped", total);
            continue;
        }

        /* ---------------------------------------------------------------
         * Address filter — ignore frames not addressed to this slave.
         * (Broadcast address 0x00 is intentionally not supported here
         *  because FC 0x03 responses to broadcasts are forbidden by
         *  the Modbus spec.)
         * --------------------------------------------------------------- */
        if (rx_buf[0] != (uint8_t)MB_SLAVE_ADDR) continue;

        ESP_LOGI(TAG, "Frame addr=%u len=%d fc=0x%02X", rx_buf[0], total, rx_buf[1]);

        /* ---------------------------------------------------------------
         * CRC validation — bad CRC → silently discard (spec §2.5.1).
         * --------------------------------------------------------------- */
        uint16_t calc_crc = crc16(rx_buf, (uint16_t)(total - 2));
        uint16_t recv_crc = (uint16_t)rx_buf[total - 2]
                          | ((uint16_t)rx_buf[total - 1] << 8U);
        if (calc_crc != recv_crc) {
            ESP_LOGW(TAG, "CRC error (got 0x%04X, expected 0x%04X) — frame dropped",
                     recv_crc, calc_crc);
            continue;
        }

        /* ---------------------------------------------------------------
         * Dispatch by function code.
         * --------------------------------------------------------------- */
        switch (rx_buf[1]) {

            case 0x03U: /* Read Holding Registers */
                ESP_LOGI(TAG, "Handling FC03 request");
                handle_fc03(rx_buf);
                break;

            default:
                send_exception(rx_buf[1], 0x01U); /* 0x01 = Illegal Function */
                break;
        }
    }
}
