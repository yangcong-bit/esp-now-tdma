# ESP-TDMA-MAC Gateway (Master) Example

This example demonstrates how to set up the **Master (Gateway)** role using the `esp_tdma_mac` component. 

The Gateway acts as the central coordinator in the TDMA network. It automatically initializes Wi-Fi (in STA mode) and ESP-NOW, registers the broadcast peer, and broadcasts synchronisation beacons every 10 ms (default). It also handles node registration and decrypts incoming sensor data packets from registered nodes using AES-128 encryption.

## How to Use

### 1. Build and Flash

Configure and compile the project using ESP-IDF:
```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### 2. Expected Output

Upon startup, you should see Wi-Fi and ESP-NOW initialization logs:
```text
I (xxx) esp_tdma_mac: Wi-Fi initialized in STA mode by esp_tdma_mac component
I (xxx) esp_tdma_mac: ESP-NOW initialized by esp_tdma_mac component
```

When a node (Slave) boots up and initiates the registration handshake, you will see registration and incoming data logs:
```text
I (xxx) gateway: Node 1 registered. Firmware version: 20260622
I (xxx) gateway: Received payload from node 1, seq=100, battery=100%, len=12
```
