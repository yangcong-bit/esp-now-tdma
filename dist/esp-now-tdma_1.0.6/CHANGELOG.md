# Changelog / 更新日志

## 1.0.6
- **Multilingual Support (多语言支持)**: Added `readme_zh.md` to support English/Chinese toggle on the registry page. *(新增 `readme_zh.md` 支持组件库网页端的英/中双语切换。)*
- **Bilingual Updates (双语日志与代码注释)**: Added Chinese comments to example codes and translated changelog. *(为示例项目代码添加了中文注释，并翻译了更新日志。)*

## 1.0.5
- **Changelog Separation (独立更新日志)**: Refactored version history into a dedicated `CHANGELOG.md` file to enable the Changelog tab in the registry. *(将版本历史整理至独立的 `CHANGELOG.md` 文件中，启用组件库独立的 Changelog 选项卡。)*

## 1.0.4
- **Registry Version Sync (组件库版本同步)**: Synchronized component version with ESP Component Registry release. *(同步代码版本号与组件库发布版。)*

## 1.0.3
- **Example Projects Added (添加示例程序)**: Added `gateway_example` and `node_example` under the `examples/` directory to show how to use the TDMA component. *(在 `examples/` 目录下新增了网关和从节点示例，展示组件的使用方法。)*
- **Example Documentation (示例说明文档)**: Added example `README.md` documentation for both Gateway and Node roles. *(为网关和从节点示例分别编写了 `README.md` 说明文件。)*

## 1.0.2
- **Homepage Migration (迁移主页及仓库)**: Migrated component homepage and repository URL to `https://github.com/yangcong-bit/esp-now-tdma`. *(迁移组件主页及开源仓库地址。)*

## 1.0.1
- **Zero-Latency Stale Frame Discarding (零延迟数据丢弃策略)**: Added backlog cleanup in the Slave's TX loop. Discards older frames and keeps only the newest frame, guaranteeing zero accumulation latency (ideal for high-rate sensors like IMUs). *(在从节点的发送循环中加入过载清理机制，自动丢弃旧帧保留最新单帧，消除高频传感器如 IMU 带来的累积延迟。)*
- **Automatic Wi-Fi & ESP-NOW Initialization (免样板无线初始化)**: Encapsulated Wi-Fi (STA mode) and ESP-NOW startup boilerplate inside `init_wifi_and_espnow()`. *(将 STA 模式 Wi-Fi 及 ESP-NOW 初始化封装进组件内部，免去应用层手动编写样板代码。)*
- **Dynamic Key Exchange & Registration (动态密钥交换与注册)**: Encapsulated `aes_lmk` in `tdma_reg_ack_t`, allowing the Master to dynamically register and modify peers with encryption upon joining. *(在注册响应包中携带 AES 局部主密钥，允许网关在节点加入时动态配置加密 Peer。)*
- **Dynamic State Synchronization (状态自动跳变)**: Slave nodes now autonomously transition to `RUNNING`, `REGISTERING`, or `OTA` based on the Master's broadcasted Beacon status (`sys_state`). *(节点根据接收到的 Beacon 系统状态标志，自动跳变至 `RUNNING`、`REGISTERING` 或 `OTA` 状态。)*
- **Console Log Flooding Prevention (控制台日志防刷限流)**: Added log throttling for `esp_now_send` failures in the Slave's TX task (logs only the first 5 errors and then throttles to once every 100 failures). *(限制发包失败日志打印频率，前 5 次错误正常输出，之后每 100 次失败才打印一次。)*
- **Enhanced Telemetry & Diagnostics (增强型遥测与链路诊断)**: Upgraded `TX Task SLOT REACHED` to output ring buffer and transmission counters (`rb_empty`, `send_ok`, `send_fail`, `reg_ack_ok/fail`), and added Master RX diagnostics logs. *(诊断日志输出新增发送计数和环形队列状态，网关端新增注册回调日志。)*
- **UTF-8 Transcoding (编码转换)**: Transcoded source files to clean UTF-8. *(将所有源文件转码为 UTF-8 编码。)*

## 1.0.0
- Initial release. *(初始版本发布。)*
