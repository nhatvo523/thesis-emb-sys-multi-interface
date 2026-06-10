#ifndef DHT22_H
#define DHT22_H

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

// ============================================================
//  Cấu hình
// ============================================================
#define DHT22_DEFAULT_GPIO      GPIO_NUM_4   // Chân DATA mặc định (D4)
#define DHT22_MAX_RETRY         3            // Số lần thử lại tối đa
#define DHT22_READ_INTERVAL_MS  2000         // Khoảng cách tối thiểu giữa 2 lần đọc (ms)

// ============================================================
//  Mã lỗi
// ============================================================
#define DHT22_OK                ESP_OK
#define DHT22_ERR_TIMEOUT       ESP_ERR_TIMEOUT
#define DHT22_ERR_CHECKSUM      ESP_ERR_INVALID_CRC
#define DHT22_ERR_INVALID_DATA  ESP_ERR_INVALID_RESPONSE

// ============================================================
//  Cấu trúc dữ liệu
// ============================================================

/**
 * @brief Kết quả đọc từ cảm biến DHT22
 */
typedef struct {
    float    temperature;   // Nhiệt độ (°C),  dải: -40 ~ +80
    float    humidity;      // Độ ẩm    (%RH), dải:   0 ~ 100
    uint32_t timestamp_ms;  // Thời điểm đọc (ms kể từ boot)
} dht22_data_t;

/**
 * @brief Handle của cảm biến DHT22
 */
typedef struct {
    gpio_num_t   gpio_pin;       // Chân GPIO kết nối DATA
    dht22_data_t last_data;      // Dữ liệu đọc gần nhất
    esp_err_t    last_err;       // Lỗi của lần đọc gần nhất
} dht22_handle_t;

// ============================================================
//  API
// ============================================================

/**
 * @brief  Khởi tạo cảm biến DHT22
 *
 * @param[out] handle   Con trỏ đến handle cần khởi tạo
 * @param[in]  gpio_pin Chân GPIO kết nối với chân DATA của DHT22
 * @return
 *   - ESP_OK nếu thành công
 *   - ESP_ERR_INVALID_ARG nếu tham số không hợp lệ
 */
esp_err_t dht22_init(dht22_handle_t *handle, gpio_num_t gpio_pin);

/**
 * @brief  Đọc nhiệt độ và độ ẩm từ cảm biến DHT22
 *
 * @param[in]  handle Con trỏ đến handle đã khởi tạo
 * @param[out] data   Con trỏ lưu kết quả đọc
 * @return
 *   - ESP_OK             nếu đọc thành công
 *   - ESP_ERR_TIMEOUT    nếu cảm biến không phản hồi
 *   - ESP_ERR_INVALID_CRC nếu checksum sai
 *   - ESP_ERR_INVALID_ARG nếu tham số NULL
 */
esp_err_t dht22_read(dht22_handle_t *handle, dht22_data_t *data);

/**
 * @brief  Đọc có thử lại tự động khi gặp lỗi
 *
 * @param[in]  handle     Con trỏ đến handle
 * @param[out] data       Kết quả đọc
 * @param[in]  max_retry  Số lần thử tối đa
 * @return ESP_OK hoặc mã lỗi sau khi hết số lần thử
 */
esp_err_t dht22_read_with_retry(dht22_handle_t *handle,
                                dht22_data_t   *data,
                                uint8_t         max_retry);

/**
 * @brief  Lấy chuỗi mô tả lỗi
 *
 * @param[in] err Mã lỗi trả về từ hàm đọc
 * @return Chuỗi mô tả lỗi (không cần giải phóng)
 */
const char *dht22_err_to_str(esp_err_t err);

#endif // DHT22_H