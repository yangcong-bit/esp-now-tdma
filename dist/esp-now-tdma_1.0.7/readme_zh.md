# ESP-TDMA-MAC 组件 (适用于 ESP-IDF)

一个开源、高性能且与数据格式无关的 TDMA（时分多址）调度 MAC 层组件，专为 ESP32-S3 上的 ESP-NOW 设计。具备超低延迟确定性通信、自适应跳频（AFH）和 NFC 频外（OOB）配网功能。

## 功能特性

- **时分多址调度 (TDMA)**：Master 节点每 10 毫秒（可配置）广播一次 Beacon 帧。最多支持 15 个 Slave 节点在分配的时隙内进行发送，完全避免了空口碰撞。
- **自适应跳频 (AFH)**：自动实时追踪每个节点的包错误率（PER）。当检测到干扰时，Master 节点会调度所有关联节点同步跳频（1 -> 6 -> 11 -> 1 频道循环）。
- **NFC 频外配网**：设计了可写入 ST25DV 动态标签邮箱（Mailbox）的快速配网数据包，支持节点与网关瞬间配对，实现零空口污染配网。
- **与应用载荷无关 (Payload Agnostic)**：MAC 层将应用数据视为不透明的字节缓冲区 (`void *`)，由用户自行定义数据结构。
- **线程安全与无锁设计**：底层采用单生产者单消费者（SPSC）环形缓冲区，确保传感器实时采集任务（如 IMU 高频采样）永远不会被无线发送任务阻塞。

---

## 快速上手

### 1. 安装组件

将 `esp_tdma_mac` 文件夹复制到您 ESP-IDF 项目的 `components` 目录下，或者在您项目的 `main/CMakeLists.txt` 中指定该路径。

确保您的组件声明依赖 `esp_wifi` 和 `espressif/esp-now`：
```cmake
REQUIRES esp_wifi espressif__esp-now
```

### 2. 菜单配置 (Kconfig)

运行 `idf.py menuconfig` 并导航至 `ESP TDMA MAC Configuration` 进行参数配置：
- `Maximum number of slave nodes` (最大从节点数，默认: 15)
- `Beacon broadcast interval` (Beacon 广播间隔，默认: 10 ms)
- `Time slot step per node` (单节点时隙跨度，默认: 700 µs)
- `PER sliding window size` (PER 滑动窗口大小，默认: 1000 包)
- `PER threshold to trigger AFH` (触发跳频的 PER 阈值，默认: 5%)
- `Maximum user payload size per packet` (单包最大用户载荷大小，默认: 220 字节)

---

## 极简示例

### 1. Master (网关端) 设置

```c
#include "esp_tdma_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "gateway";

// 接收到从节点数据的回调函数
static void on_data_received(uint8_t node_id, uint32_t seq, uint8_t battery, 
                             const void *payload, uint8_t payload_len) {
    ESP_LOGI(TAG, "从节点 %d 收到载荷, 序列号=%lu, 电量=%d%%, 长度=%d",
             node_id, (unsigned long)seq, battery, payload_len);
    // 可在此处将 payload 强转回您自定义的结构体
    // my_sensor_data_t *data = (my_sensor_data_t *)payload;
}

// 节点成功注册的回调函数
static void on_node_registered(uint8_t node_id, uint32_t fw_ver, bool upgrade_required) {
    ESP_LOGI(TAG, "节点 %d 注册成功. 固件版本: %lu", node_id, (unsigned long)fw_ver);
}

void app_main(void) {
    // 初始化 NVS 闪存（Wi-Fi 存储必需）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Wi-Fi（STA模式）和 ESP-NOW 将由组件在启动时自动初始化。
    // 无需在应用层编写繁琐的 Wi-Fi/ESP-NOW 初始化样板代码。
    
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

### 2. Slave (从节点端) 设置

```c
#include "esp_tdma_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "node";

void app_main(void) {
    // 初始化 NVS 闪存（Wi-Fi 存储必需）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Wi-Fi（STA模式）和 ESP-NOW 将由组件在启动时自动初始化。
    // 无需在应用层编写繁琐的 Wi-Fi/ESP-NOW 初始化样板代码。
    
    esp_tdma_slave_cfg_t cfg = {
        .on_state_changed = NULL
    };
    
    ESP_ERROR_CHECK(esp_tdma_slave_init(&cfg));
    
    // 配置信息（可通过 NFC 频外获取或从 NVS 中加载）：
    uint8_t node_id = 1;
    uint8_t gateway_mac[6] = {0x28, 0x84, 0x85, 0x52, 0xC4, 0x0C};
    uint8_t aes_lmk[16] = {0}; // 用于加密 ESP-NOW 的局部主密钥
    uint32_t slot_offset_us = node_id * 700; // 时隙偏移 (如 700 微秒)
    
    esp_tdma_slave_set_config(node_id, gateway_mac, aes_lmk, slot_offset_us);
    ESP_ERROR_CHECK(esp_tdma_slave_start());
    
    // 在您的传感器采样任务中：
    while (1) {
        my_sensor_data_t data = read_sensor();
        // 将采集到的数据压入发送队列（非阻塞）
        esp_tdma_slave_enqueue(&data, sizeof(data));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## 许可协议

本项目基于 Apache License 2.0 许可证开源 - 详情见 `LICENSE` 文件。
