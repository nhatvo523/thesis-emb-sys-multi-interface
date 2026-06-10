#include <Arduino.h>
// #include <WiFi.h>          // disabled — web removed
// #include "uart_task.h"     // disabled — uart removed
// #include "web_task.h"      // disabled — web removed
#include "modbus_slave.h"
#include "esp_log.h"
#include "dht22.h"
#include <stdlib.h>   /* rand() */

static const char* TAG = "MAIN";

// DHT22 sensor handle
static dht22_handle_t dht22_sensor;

/* Periodic task: read DHT22 sensor data and push to Modbus registers.
 *   Reg 0: Temperature × 10  (e.g., 25.3 °C  →  253)
 *   Reg 1: Humidity    × 10  (e.g., 60.1 %RH  →  601)
 *   Reg 2: Digital inputs bitmask — always 0 (no real inputs)
 *   Reg 3–7: Reserved, 0
 */
static void sensor_Task(void* pvParameters)
{
    (void)pvParameters;
    
    dht22_data_t sensor_data;
    
    for (;;) {
        // Read DHT22 sensor with retry
        esp_err_t err = dht22_read_with_retry(&dht22_sensor, &sensor_data, DHT22_MAX_RETRY);
        
        uint16_t mb_regs[MB_REG_COUNT] = {0};
        
        if (err == ESP_OK) {
            // Convert to Modbus format (×10)
            mb_regs[0] = (uint16_t)(sensor_data.temperature * 10.0f + 0.5f);  // Temperature × 10
            mb_regs[1] = (uint16_t)(sensor_data.humidity * 10.0f + 0.5f);     // Humidity × 10
            mb_regs[2] = 0;  // Digital inputs bitmask
            // Reg 3-7: already 0 from initialization
            
            modbusSlave_UpdateRegs(mb_regs);
            
            // Log to ESP_LOG
            ESP_LOGI(TAG, "DHT22: temp=%.1f°C  hum=%.1f%%RH  (MB: %u  %u)",
                     sensor_data.temperature, sensor_data.humidity,
                     mb_regs[0], mb_regs[1]);
            
            // Print to terminal (Serial)
            Serial.print("\n======== DHT22 SENSOR DATA ========\n");
            Serial.print("Temperature: ");
            Serial.print(sensor_data.temperature, 1);
            Serial.println(" °C");
            Serial.print("Humidity:    ");
            Serial.print(sensor_data.humidity, 1);
            Serial.println(" %RH");
            Serial.print("Timestamp:   ");
            Serial.print(sensor_data.timestamp_ms);
            Serial.println(" ms");
            Serial.print("Modbus Reg0: ");
            Serial.println(mb_regs[0]);
            Serial.print("Modbus Reg1: ");
            Serial.println(mb_regs[1]);
            Serial.println("====================================\n");
        } else {
            ESP_LOGW(TAG, "DHT22 read failed: %s — using last known values",
                     dht22_err_to_str(err));
            // Use last known values from sensor handle
            mb_regs[0] = (uint16_t)(dht22_sensor.last_data.temperature * 10.0f + 0.5f);
            mb_regs[1] = (uint16_t)(dht22_sensor.last_data.humidity * 10.0f + 0.5f);
            modbusSlave_UpdateRegs(mb_regs);
            
            // Print error to terminal
            Serial.print("\n[ERROR] DHT22 read failed: ");
            Serial.println(dht22_err_to_str(err));
            Serial.print("Using last known values - Temp: ");
            Serial.print(dht22_sensor.last_data.temperature, 1);
            Serial.print("°C  |  Hum: ");
            Serial.print(dht22_sensor.last_data.humidity, 1);
            Serial.println("%RH\n");
        }
        
        // Wait before next read (DHT22 requires ≥2s between reads)
        vTaskDelay(pdMS_TO_TICKS(DHT22_READ_INTERVAL_MS));
    }
}

void setup() {
    Serial.begin(9600);
    delay(1000);

    Serial.println("\n\n=== ESP32 Modbus Slave with DHT22 ===");

    // Initialize Modbus slave
    modbusSlave_Init();

    // Initialize DHT22 sensor on GPIO4
    esp_err_t err = dht22_init(&dht22_sensor, DHT22_DEFAULT_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DHT22 init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "DHT22 initialized on GPIO%d", DHT22_DEFAULT_GPIO);
    }

    xTaskCreatePinnedToCore(modbusSlave_Task, "MB_SLAVE", MB_TASK_STACK,  NULL, MB_TASK_PRIORITY, NULL, 1);
    xTaskCreatePinnedToCore(sensor_Task,      "SENSOR",   2048,           NULL, 2,                NULL, 0);

    Serial.println("[*] Tasks started");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}

