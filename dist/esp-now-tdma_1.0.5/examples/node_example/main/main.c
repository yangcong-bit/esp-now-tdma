#include "esp_tdma_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "node";

typedef struct {
    float x;
    float y;
    float z;
} dummy_sensor_data_t;

static void sensor_task(void *pvParameters) {
    dummy_sensor_data_t data = {1.0f, 2.0f, 3.0f};
    while (1) {
        // Enqueue sensor data to TDMA ring buffer SPSC queue
        esp_tdma_slave_enqueue(&data, sizeof(data));
        ESP_LOGI(TAG, "Enqueued dummy sensor data: x=%.2f, y=%.2f, z=%.2f", data.x, data.y, data.z);
        
        data.x += 0.1f;
        data.y += 0.2f;
        data.z += 0.3f;
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Sample every 100ms
    }
}

void app_main(void) {
    // Initialise NVS (required for Wi-Fi storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Wi-Fi (STA mode) and ESP-NOW are automatically initialized by the component on startup.
    // No manual Wi-Fi or ESP-NOW initialization boilerplate required.
    
    esp_tdma_slave_cfg_t cfg = {
        .on_state_changed = NULL
    };
    
    ESP_ERROR_CHECK(esp_tdma_slave_init(&cfg));
    
    // Config received via NFC or loaded from NVS:
    uint8_t node_id = 1;
    uint8_t gateway_mac[6] = {0x28, 0x84, 0x85, 0x52, 0xC4, 0x0C};
    uint8_t aes_lmk[16] = {0}; // LMK for encrypted ESP-NOW
    uint32_t slot_offset_us = node_id * 700; // 700us offset
    
    esp_tdma_slave_set_config(node_id, gateway_mac, aes_lmk, slot_offset_us);
    ESP_ERROR_CHECK(esp_tdma_slave_start());
    
    // Start sensor dummy sampling task
    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 5, NULL);
}
