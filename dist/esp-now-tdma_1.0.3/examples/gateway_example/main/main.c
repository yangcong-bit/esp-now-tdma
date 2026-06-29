#include "esp_tdma_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "gateway";

static void on_data_received(uint8_t node_id, uint32_t seq, uint8_t battery, 
                             const void *payload, uint8_t payload_len) {
    ESP_LOGI(TAG, "Received payload from node %d, seq=%lu, battery=%d%%, len=%d",
             node_id, (unsigned long)seq, battery, payload_len);
}

static void on_node_registered(uint8_t node_id, uint32_t fw_ver, bool upgrade_required) {
    ESP_LOGI(TAG, "Node %d registered. Firmware version: %lu", node_id, (unsigned long)fw_ver);
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
    
    esp_tdma_master_cfg_t cfg = {
        .target_node_count = 1,
        .on_data_received = on_data_received,
        .on_node_registered = on_node_registered,
        .on_state_changed = NULL
    };
    
    ESP_ERROR_CHECK(esp_tdma_master_init(&cfg));
    ESP_ERROR_CHECK(esp_tdma_master_start());
}
