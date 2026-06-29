# Changelog

## 1.0.5
- Refactored version history into a dedicated CHANGELOG.md file to enable the Changelog tab in the registry.

## 1.0.4
- Synchronized component version with ESP Component Registry release.

## 1.0.3
- Added `gateway_example` and `node_example` under the `examples/` directory to show how to use the TDMA component.
- Added example `README.md` documentation for both Gateway and Node roles.

## 1.0.2
- Migrated component homepage and repository URL to `https://github.com/yangcong-bit/esp-now-tdma`.

## 1.0.1
- **Zero-Latency Stale Frame Discarding**: Added backlog cleanup in the Slave's TX loop. Discards older frames and keeps only the newest frame, guaranteeing zero accumulation latency (ideal for high-rate sensors like IMUs).
- **Automatic Wi-Fi & ESP-NOW Initialization**: Encapsulated Wi-Fi (STA mode) and ESP-NOW startup boilerplate inside `init_wifi_and_espnow()`.
- **Dynamic Key Exchange & Registration**: Encapsulated `aes_lmk` in `tdma_reg_ack_t`, allowing the Master to dynamically register and modify peers with encryption upon joining.
- **Dynamic State Synchronization**: Slave nodes now autonomously transition to `RUNNING`, `REGISTERING`, or `OTA` based on the Master's broadcasted Beacon status (`sys_state`).
- **Console Log Flooding Prevention**: Added log throttling for `esp_now_send` failures in the Slave's TX task (logs only the first 5 errors and then throttles to once every 100 failures).
- **Enhanced Telemetry & Diagnostics**: Upgraded `TX Task SLOT REACHED` to output ring buffer and transmission counters (`rb_empty`, `send_ok`, `send_fail`, `reg_ack_ok/fail`), and added Master RX diagnostics logs.
- **UTF-8 Transcoding**: Transcoded source files to clean UTF-8.

## 1.0.0
- Initial release.
