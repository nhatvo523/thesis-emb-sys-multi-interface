/**
 * @file    dht22.c
 * @brief   Driver đọc cảm biến DHT22 trên ESP32 DevKit 1
 *
 * Giao thức: single-wire, 1 lần đọc = 40 bit (5 byte)
 *   Byte 0: Phần nguyên độ ẩm
 *   Byte 1: Phần thập phân độ ẩm
 *   Byte 2: Phần nguyên nhiệt độ (bit 15 = dấu âm)
 *   Byte 3: Phần thập phân nhiệt độ
 *   Byte 4: Checksum = (Byte0 + Byte1 + Byte2 + Byte3) & 0xFF
 *
 * Sơ đồ nối:
 *   DHT22 VCC  -> 3.3V (hoặc 5V nếu module có bộ hạ áp)
 *   DHT22 DATA -> GPIO (mặc định GPIO4), điện trở kéo lên 4.7kΩ -> 3.3V
 *   DHT22 GND  -> GND
 *
 * Ví dụ sử dụng:
 * @code
 *   dht22_handle_t sensor;
 *   dht22_init(&sensor, GPIO_NUM_4);
 *
 *   dht22_data_t result;
 *   esp_err_t err = dht22_read_with_retry(&sensor, &result, DHT22_MAX_RETRY);
 *   if (err == ESP_OK) {
 *       printf("Nhiệt độ: %.1f °C  |  Độ ẩm: %.1f %%RH\n",
 *              result.temperature, result.humidity);
 *   } else {
 *       printf("Lỗi: %s\n", dht22_err_to_str(err));
 *   }
 * @endcode
 */

#include "dht22.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

// ============================================================
//  Hằng số nội bộ
// ============================================================
static const char *TAG = "DHT22";

#define DHT22_START_LOW_US      1100   // Tín hiệu bắt đầu: kéo thấp >= 1ms
#define DHT22_START_HIGH_US     30     // Sau đó kéo cao ~20-40 µs
#define DHT22_WAIT_TIMEOUT_US   100    // Timeout chờ mỗi pha tín hiệu
#define DHT22_BIT_ONE_THRESHOLD 50     // Nếu pulse HIGH > 50µs => bit 1

// ============================================================
//  Tiện ích nội bộ
// ============================================================

/**
 * @brief Đo thời gian tín hiệu ở mức `level` cho đến khi đổi trạng thái.
 * @return Số micro-giây đo được, hoặc -1 nếu timeout.
 */
static int32_t dht22_wait_level(gpio_num_t pin, uint8_t level, uint32_t timeout_us)
{
    uint32_t start = (uint32_t)(esp_timer_get_time());
    while (gpio_get_level(pin) != level) {
        if ((uint32_t)(esp_timer_get_time()) - start > timeout_us) {
            return -1;
        }
    }
    uint32_t t0 = (uint32_t)(esp_timer_get_time());
    while (gpio_get_level(pin) == level) {
        if ((uint32_t)(esp_timer_get_time()) - t0 > timeout_us) {
            return -1;
        }
    }
    return (int32_t)((uint32_t)(esp_timer_get_time()) - t0);
}

// ============================================================
//  Triển khai API
// ============================================================

esp_err_t dht22_init(dht22_handle_t *handle, gpio_num_t gpio_pin)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle, 0, sizeof(dht22_handle_t));
    handle->gpio_pin = gpio_pin;
    handle->last_err = ESP_OK;

    // Cấu hình GPIO: ban đầu ở chế độ output, mức HIGH (idle)
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config thất bại: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(gpio_pin, 1);

    ESP_LOGI(TAG, "Khởi tạo DHT22 trên GPIO%d thành công", gpio_pin);
    return ESP_OK;
}

esp_err_t dht22_read(dht22_handle_t *handle, dht22_data_t *data)
{
    if (handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_num_t pin = handle->gpio_pin;
    uint8_t    raw[5] = {0};
    esp_err_t  err    = ESP_OK;

    // ----------------------------------------------------------
    // 1. Gửi tín hiệu bắt đầu (Start Signal)
    //    MCU kéo DATA thấp >= 1ms, sau đó kéo cao 20-40µs
    // ----------------------------------------------------------
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    ets_delay_us(DHT22_START_LOW_US);
    gpio_set_level(pin, 1);
    ets_delay_us(DHT22_START_HIGH_US);

    // ----------------------------------------------------------
    // 2. Chuyển GPIO sang INPUT, chờ DHT22 phản hồi
    //    DHT22 kéo thấp ~80µs, rồi kéo cao ~80µs
    // ----------------------------------------------------------
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    // Chờ DHT22 kéo xuống LOW
    if (dht22_wait_level(pin, 0, DHT22_WAIT_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "Timeout chờ phản hồi DHT22 (LOW)");
        err = DHT22_ERR_TIMEOUT;
        goto cleanup;
    }
    // Chờ DHT22 kéo lên HIGH
    if (dht22_wait_level(pin, 1, DHT22_WAIT_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "Timeout chờ phản hồi DHT22 (HIGH)");
        err = DHT22_ERR_TIMEOUT;
        goto cleanup;
    }

    // ----------------------------------------------------------
    // 3. Đọc 40 bit dữ liệu
    //    Mỗi bit: DHT22 kéo LOW ~50µs, sau đó kéo HIGH
    //      HIGH ~26-28µs => bit 0
    //      HIGH ~70µs    => bit 1
    // ----------------------------------------------------------
    for (int i = 0; i < 40; i++) {
        // Chờ cạnh xuống (bắt đầu bit)
        int32_t low_us = dht22_wait_level(pin, 0, DHT22_WAIT_TIMEOUT_US);
        if (low_us < 0) {
            ESP_LOGW(TAG, "Timeout đọc bit %d (LOW phase)", i);
            err = DHT22_ERR_TIMEOUT;
            goto cleanup;
        }

        // Đo thời gian HIGH để phân biệt bit 0 / bit 1
        int32_t high_us = dht22_wait_level(pin, 1, DHT22_WAIT_TIMEOUT_US);
        if (high_us < 0) {
            ESP_LOGW(TAG, "Timeout đọc bit %d (HIGH phase)", i);
            err = DHT22_ERR_TIMEOUT;
            goto cleanup;
        }

        raw[i / 8] <<= 1;
        if (high_us > DHT22_BIT_ONE_THRESHOLD) {
            raw[i / 8] |= 0x01;  // Bit 1
        }
        // Bit 0: không set gì
    }

    // ----------------------------------------------------------
    // 4. Kiểm tra checksum
    // ----------------------------------------------------------
    {
        uint8_t sum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
        if (sum != raw[4]) {
            ESP_LOGW(TAG, "Checksum lỗi: tính được 0x%02X, nhận 0x%02X", sum, raw[4]);
            err = DHT22_ERR_CHECKSUM;
            goto cleanup;
        }
    }

    // ----------------------------------------------------------
    // 5. Giải mã dữ liệu
    // ----------------------------------------------------------
    {
        // Độ ẩm
        uint16_t raw_hum = ((uint16_t)raw[0] << 8) | raw[1];
        data->humidity   = (float)raw_hum / 10.0f;

        // Nhiệt độ (bit 15 của byte 2 là dấu âm)
        uint16_t raw_temp  = (uint16_t)(raw[2] & 0x7F) << 8 | raw[3];
        data->temperature  = (float)raw_temp / 10.0f;
        if (raw[2] & 0x80) {
            data->temperature = -data->temperature;
        }

        data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

        ESP_LOGI(TAG, "Đọc OK — Nhiệt độ: %.1f °C | Độ ẩm: %.1f %%RH",
                 data->temperature, data->humidity);
    }

cleanup:
    // Đưa GPIO về trạng thái idle (HIGH)
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);

    handle->last_err = err;
    if (err == ESP_OK) {
        handle->last_data = *data;
    }
    return err;
}

esp_err_t dht22_read_with_retry(dht22_handle_t *handle,
                                dht22_data_t   *data,
                                uint8_t         max_retry)
{
    if (handle == NULL || data == NULL || max_retry == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_FAIL;
    for (uint8_t i = 0; i < max_retry; i++) {
        err = dht22_read(handle, data);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Thử lần %d/%d thất bại: %s", i + 1, max_retry,
                 dht22_err_to_str(err));

        // DHT22 cần ít nhất 2 giây giữa 2 lần đọc
        vTaskDelay(pdMS_TO_TICKS(DHT22_READ_INTERVAL_MS));
    }
    ESP_LOGE(TAG, "Đọc thất bại sau %d lần thử", max_retry);
    return err;
}

const char *dht22_err_to_str(esp_err_t err)
{
    switch (err) {
        case ESP_OK:                    return "Thành công";
        case ESP_ERR_TIMEOUT:           return "Cảm biến không phản hồi (timeout)";
        case ESP_ERR_INVALID_CRC:       return "Lỗi checksum dữ liệu";
        case ESP_ERR_INVALID_RESPONSE:  return "Dữ liệu không hợp lệ";
        case ESP_ERR_INVALID_ARG:       return "Tham số không hợp lệ";
        default:                        return esp_err_to_name(err);
    }
}