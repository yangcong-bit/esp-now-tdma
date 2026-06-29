# ESP-TDMA-MAC Node (Slave) Example

This example demonstrates how to set up a **Slave (Node)** role using the `esp_tdma_mac` component.

The Node synchronizes its transmission slots based on the beacons received from the Master (Gateway). It automatically initializes Wi-Fi and ESP-NOW, listens for beacons, dynamically transitions its state, and enqueues sensor data into a lock-free ring buffer for zero-latency transmission.

## How to Use

### 1. Build and Flash

Configure and compile the project using ESP-IDF:
```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### 2. Expected Output

Upon startup, the Node will search for beacons. Once a beacon is received, the Node transitions to `RUNNING` and starts enqueuing and sending data:
```text
W (xxx) esp_tdma_mac: Slave: state transitioned to RUNNING based on Beacon
I (xxx) node: Enqueued dummy sensor data: x=1.00, y=2.00, z=3.00
```

Every 100 frames, it prints telemetry counters to evaluate wireless channel quality:
```text
W (xxx) esp_tdma_mac: TX Task SLOT REACHED! Seq: 101, State: 2, Offset: 700 | rb_empty=0 send_fail=0 send_ok=100 reg_ack_ok=5 reg_ack_fail=0
```
