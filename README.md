# ESP-TDMA-MAC Component for ESP-IDF

An open-source, high-performance, and payload-agnostic TDMA (Time Division Multiple Access) scheduled MAC layer for ESP-NOW on ESP32-S3. Features low-latency deterministic communication with Adaptive Frequency Hopping (AFH) and NFC Out-Of-Band (OOB) provisioning.

## Features

- **Time Division scheduled (TDMA)**: Master broadcasts a beacon every 10 ms (configurable). Up to 15 slaves transmit in assigned slots, completely eliminating collisions.
- **Adaptive Frequency Hopping (AFH)**: Automatic real-time Packet Error Rate (PER) tracking per node. When interference is detected, the master schedules a synchronized hop across channels (1 -> 6 -> 11 -> 1).
- **NFC Out-Of-Band Provisioning**: Fast provisioning payloads designed to be written to ST25DV mailbox. Allows nodes to pair instantly with zero radio pollution.
- **Payload Agnostic**: The MAC layer treats the payload as an opaque byte buffer (`void *`). You define your own structures.
- **Thread-safe & Lock-free**: Uses a Single-Producer Single-Consumer (SPSC) ring buffer under the hood, ensuring that real-time sensor processing (e.g., IMU sampling) is never blocked by transmission.

---

## Getting Started

### 1. Installation

Copy the `esp_tdma_mac` folder into your ESP-IDF project's `components` directory or specify it in your `main/CMakeLists.txt`.

Ensure your component requires `esp_wifi` and `espressif/esp-now`:
```cmake
REQUIRES esp_wifi espressif__esp-now
```

### 2. Configuration (Kconfig)

Run `idf.py menuconfig` and navigate to `ESP TDMA MAC Configuration`:
- `Maximum number of slave nodes` (Default: 15)
- `Beacon broadcast interval` (Default: 10 ms)
- `Time slot step per node` (Default: 700 µs)
- `PER sliding window size` (Default: 1000 packets)
- `PER threshold to trigger AFH` (Default: 5%)
- `Maximum user payload size per packet` (Default: 220 bytes)

---

## Quick Example

### Master (Gateway) Setup

```c
#include "esp_tdma_mac.h"
#include "esp_log.h"

static const char *TAG = "gateway";

static void on_data_received(uint8_t node_id, uint32_t seq, uint8_t battery, 
                             const void *payload, uint8_t payload_len) {
    ESP_LOGI(TAG, "Received payload from node %d, seq=%lu, battery=%d%%, len=%d",
             node_id, (unsigned long)seq, battery, payload_len);
    // Cast payload back to your custom struct
    // my_sensor_data_t *data = (my_sensor_data_t *)payload;
}

static void on_node_registered(uint8_t node_id, uint32_t fw_ver, bool upgrade_required) {
    ESP_LOGI(TAG, "Node %d registered. Firmware version: %lu", node_id, (unsigned long)fw_ver);
}

void app_main(void) {
    // Initialise Wi-Fi in STA mode & ESP-NOW first
    // ...
    
    esp_tdma_master_cfg_t cfg = {
        .target_node_count = 1,
        .on_data_received = on_data_received,
        .on_node_registered = on_node_registered,
        .on_state_changed = NULL
    };
    
    ESP_ERROR_CHECK(esp_tdma_master_init(&cfg));
    ESP_ERROR_CHECK(esp_tdma_master_start());
}
```

### Slave (Node) Setup

```c
#include "esp_tdma_mac.h"
#include "esp_log.h"

static const char *TAG = "node";

void app_main(void) {
    // Initialise Wi-Fi in STA mode & ESP-NOW first
    // ...
    
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
    
    // In your sensor sampling task:
    while (1) {
        my_sensor_data_t data = read_sensor();
        esp_tdma_slave_enqueue(&data, sizeof(data));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

## License

This project is licensed under the Apache License 2.0 - see the `LICENSE` file for details.
