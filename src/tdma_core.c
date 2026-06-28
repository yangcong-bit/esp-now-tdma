/**
 * @file tdma_core.c
 * @brief Core TDMA engine implementation for both master and slave roles.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2024 SomatoSync Contributors
 */
#include "esp_tdma_mac.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "esp_tdma_mac";

// --- Global States ---
static esp_tdma_state_t s_master_state = TDMA_STATE_IDLE;
static esp_tdma_state_t s_slave_state  = TDMA_STATE_IDLE;

// --- Forward Declarations ---
static void master_handle_rx(const uint8_t *src_mac, const uint8_t *data, int len);
static void slave_handle_rx(const uint8_t *src_mac, const uint8_t *data, int len);
static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len);

// ============================================================================
// MASTER implementation
// ============================================================================

#define MAX_NODES        CONFIG_TDMA_MAX_NODES
#define AFH_WINDOW_SIZE  CONFIG_TDMA_PER_WINDOW_SIZE

typedef struct {
    bool     is_registered;
    uint8_t  node_id;
    uint8_t  mac_addr[6];
    uint32_t current_fw_ver;
    bool     fw_needs_upgrade;
    uint32_t last_heartbeat;
} master_node_record_t;

static master_node_record_t s_node_registry[MAX_NODES] = {0};
static esp_tdma_master_cfg_t s_master_cfg = {0};

static esp_timer_handle_t s_master_beacon_timer = NULL;
static uint32_t s_master_global_frame_seq = 0;

// AFH state
static uint8_t s_master_afh_target_channel  = 0;
static uint8_t s_master_afh_countdown       = 0;
static bool    s_master_afh_pending_switch  = false;

// PER sliding-window per node
static uint8_t  rx_history[MAX_NODES][AFH_WINDOW_SIZE] = {0};
static uint32_t running_lost[MAX_NODES]     = {0};
static uint32_t running_received[MAX_NODES] = {0};
static uint32_t last_packet_seq[MAX_NODES]  = {0};
static uint32_t eval_counter[MAX_NODES]     = {0};

// Slot-jitter measurement
static volatile int64_t s_master_last_beacon_us = 0;
#define SLOT_STEP_US    CONFIG_TDMA_SLOT_STEP_US
#define JITTER_WINDOW   100
static int32_t  jitter_samples[MAX_NODES][JITTER_WINDOW] = {0};
static uint32_t jitter_write_idx[MAX_NODES]    = {0};
static uint32_t jitter_sample_count[MAX_NODES] = {0};
static int32_t  jitter_max[MAX_NODES]          = {0};
static int32_t  jitter_min[MAX_NODES]          = {0};

esp_tdma_state_t esp_tdma_master_get_state(void) {
    return s_master_state;
}

void esp_tdma_master_set_state(esp_tdma_state_t state) {
    s_master_state = state;
    if (s_master_cfg.on_state_changed) {
        s_master_cfg.on_state_changed(state);
    }
}

void esp_tdma_master_register_node(uint8_t node_id, const uint8_t *mac_addr,
                                   const uint8_t *aes_lmk, uint32_t fw_ver,
                                   bool needs_upgrade) {
    if (node_id == 0 || node_id > MAX_NODES) return;
    int idx = node_id - 1;
    s_node_registry[idx].is_registered   = true;
    s_node_registry[idx].node_id         = node_id;
    s_node_registry[idx].current_fw_ver  = fw_ver;
    s_node_registry[idx].fw_needs_upgrade = needs_upgrade;
    memcpy(s_node_registry[idx].mac_addr, mac_addr, 6);

    if (!esp_now_is_peer_exist(mac_addr)) {
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, mac_addr, 6);
        peer.channel = 0;
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = (aes_lmk != NULL);
        if (aes_lmk) memcpy(peer.lmk, aes_lmk, 16);
        esp_now_add_peer(&peer);
    }
}

void esp_tdma_master_trigger_afh(uint8_t target_channel) {
    if (s_master_afh_pending_switch) return;
    s_master_afh_target_channel  = target_channel;
    s_master_afh_countdown       = 10;
    s_master_afh_pending_switch  = true;
    ESP_LOGW(TAG, "AFH scheduled: hop to channel %d in 10 beacons", target_channel);
}

// Master beacon timer callback
static void master_beacon_timer_cb(void *arg) {
    uint32_t frame_seq = s_master_global_frame_seq++;

    uint8_t next_channel  = 0;
    uint8_t countdown     = 0;

    if (s_master_afh_pending_switch) {
        s_master_afh_countdown--;
        next_channel = s_master_afh_target_channel;
        countdown    = s_master_afh_countdown;

        if (s_master_afh_countdown == 0) {
            s_master_afh_pending_switch = false;
            ESP_LOGW(TAG, "AFH: switching to channel %d NOW", s_master_afh_target_channel);
            esp_wifi_set_channel(s_master_afh_target_channel, WIFI_SECOND_CHAN_NONE);
        }
    }

    uint16_t bitmask = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_node_registry[i].is_registered) bitmask |= (1u << i);
    }

    tdma_beacon_t beacon = {
        .type                 = TDMA_PKT_BEACON,
        .frame_seq            = frame_seq,
        .active_nodes_bitmask = bitmask,
        .sys_state            = (uint8_t)s_master_state,
        .next_channel         = next_channel,
        .switch_countdown     = countdown
    };
    uint64_t t_now = esp_timer_get_time();
    memcpy(&beacon.global_time_us, &t_now, sizeof(t_now));

    s_master_last_beacon_us = esp_timer_get_time();
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcast, (uint8_t *)&beacon, sizeof(beacon));

    if (frame_seq % 500 == 0) {
        ESP_LOGI(TAG, "Beacon alive: seq=%lu state=%d", (unsigned long)frame_seq,
                 (int)s_master_state);
    }
}

// Master RX handler
static void master_handle_rx(const uint8_t *src_mac, const uint8_t *data, int len) {
    if (len < 1) return;
    uint8_t pkt_type = data[0];

    if (pkt_type == TDMA_PKT_DATA && len >= (int)(sizeof(tdma_data_pkt_t) - CONFIG_TDMA_PAYLOAD_SIZE)) {
        int64_t rx_us = esp_timer_get_time();
        const tdma_data_pkt_t *pkt = (const tdma_data_pkt_t *)data;

        uint8_t  node_id = pkt->node_id;
        uint8_t  battery = pkt->battery_pct;
        uint8_t  payload_len = pkt->payload_len;
        uint32_t packet_seq;
        memcpy(&packet_seq, &pkt->packet_seq, sizeof(packet_seq));

        if (node_id == 0 || node_id > MAX_NODES) return;
        int idx = node_id - 1;
        s_node_registry[idx].last_heartbeat = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Slot jitter measurement
        int64_t beacon_us = s_master_last_beacon_us;
        if (beacon_us > 0) {
            int64_t actual_us   = rx_us - beacon_us;
            int32_t theory_us   = (int32_t)(node_id * SLOT_STEP_US);
            int32_t jitter_us   = (int32_t)(actual_us - theory_us);

            uint32_t widx = jitter_write_idx[idx];
            jitter_samples[idx][widx] = jitter_us;
            jitter_write_idx[idx] = (widx + 1) % JITTER_WINDOW;
            if (jitter_sample_count[idx] < JITTER_WINDOW) jitter_sample_count[idx]++;

            if (jitter_sample_count[idx] == 1) {
                jitter_max[idx] = jitter_min[idx] = jitter_us;
            } else {
                if (jitter_us > jitter_max[idx]) jitter_max[idx] = jitter_us;
                if (jitter_us < jitter_min[idx]) jitter_min[idx] = jitter_us;
            }
        }

        // PER sliding window
        if (s_master_state == TDMA_STATE_RUNNING) {
            uint32_t seq = packet_seq;
            if (last_packet_seq[idx] == 0) {
                last_packet_seq[idx]   = seq;
                rx_history[idx][seq % AFH_WINDOW_SIZE] = 1;
                running_received[idx]  = 1;
                running_lost[idx]      = 0;
            } else if (seq > last_packet_seq[idx]) {
                uint32_t gap = seq - last_packet_seq[idx];
                if (gap > AFH_WINDOW_SIZE) {
                    memset(rx_history[idx], 0, AFH_WINDOW_SIZE);
                    running_received[idx] = 1;
                    running_lost[idx]     = AFH_WINDOW_SIZE - 1;
                    rx_history[idx][seq % AFH_WINDOW_SIZE] = 1;
                } else {
                    for (uint32_t s = last_packet_seq[idx] + 1; s < seq; s++) {
                        uint32_t si = s % AFH_WINDOW_SIZE;
                        if (rx_history[idx][si] == 1) {
                            if (running_received[idx] > 0) running_received[idx]--;
                        } else {
                            if (running_lost[idx] > 0) running_lost[idx]--;
                        }
                        rx_history[idx][si] = 0;
                        running_lost[idx]++;
                    }
                    uint32_t si = seq % AFH_WINDOW_SIZE;
                    if (rx_history[idx][si] == 1) {
                        if (running_received[idx] > 0) running_received[idx]--;
                    } else {
                        if (running_lost[idx] > 0) running_lost[idx]--;
                    }
                    rx_history[idx][si] = 1;
                    running_received[idx]++;
                }
                last_packet_seq[idx] = seq;
            }

            uint32_t total = running_received[idx] + running_lost[idx];
            eval_counter[idx]++;

            if (total >= AFH_WINDOW_SIZE && eval_counter[idx] % 100 == 0) {
                float per = (float)running_lost[idx] / AFH_WINDOW_SIZE;

                // Compute jitter mean
                int32_t jitter_mean = 0;
                uint32_t n = jitter_sample_count[idx];
                if (n > 0) {
                    int64_t sum = 0;
                    for (uint32_t ji = 0; ji < n; ji++) sum += jitter_samples[idx][ji];
                    jitter_mean = (int32_t)(sum / (int64_t)n);
                }
                ESP_LOGI(TAG,
                         "Node %d 10s window: Recv=%lu Lost=%lu PER=%.2f%% | "
                         "Jitter mean=%+ldus max=%+ldus min=%+ldus (theory=%dus)",
                         node_id,
                         (unsigned long)running_received[idx],
                         (unsigned long)running_lost[idx],
                         per * 100.0f,
                         (long)jitter_mean, (long)jitter_max[idx], (long)jitter_min[idx],
                         node_id * SLOT_STEP_US);

                if (per > (CONFIG_TDMA_PER_THRESHOLD_PCT / 100.0f)) {
                    uint8_t cur_chan = 1;
                    wifi_second_chan_t second;
                    esp_wifi_get_channel(&cur_chan, &second);
                    uint8_t tgt = (cur_chan == 1) ? 6 : (cur_chan == 6) ? 11 : 1;
                    ESP_LOGW(TAG, "Node %d PER %.2f%% > %d%% on ch%d — hopping to ch%d",
                             node_id, per * 100.0f, CONFIG_TDMA_PER_THRESHOLD_PCT,
                             cur_chan, tgt);
                    esp_tdma_master_trigger_afh(tgt);
                    for (int i = 0; i < MAX_NODES; i++) {
                        memset(rx_history[i], 0, AFH_WINDOW_SIZE);
                        running_lost[i] = running_received[i] = last_packet_seq[i] = eval_counter[i] = 0;
                    }
                }
            }
        }

        // Notify application
        if (s_master_cfg.on_data_received) {
            s_master_cfg.on_data_received(node_id, packet_seq, battery,
                                          pkt->payload, payload_len);
        }

    } else if (pkt_type == TDMA_PKT_REG_ACK && len == sizeof(tdma_reg_ack_t)) {
        const tdma_reg_ack_t *ack = (const tdma_reg_ack_t *)data;
        uint8_t nid = ack->node_id;
        uint32_t fw;
        memcpy(&fw, &ack->current_fw_ver, sizeof(fw));
        bool needs_up = ack->fw_needs_upgrade;

        if (nid > 0 && nid <= MAX_NODES) {
            s_node_registry[nid - 1].is_registered  = true;
            s_node_registry[nid - 1].current_fw_ver = fw;
        }
        if (s_master_cfg.on_node_registered) {
            s_master_cfg.on_node_registered(nid, fw, needs_up);
        }

        // Check if all expected nodes are present
        if (s_master_state == TDMA_STATE_REGISTERING) {
            uint8_t count = 0;
            for (int i = 0; i < MAX_NODES; i++) {
                if (s_node_registry[i].is_registered) count++;
            }
            if (count >= s_master_cfg.target_node_count) {
                esp_tdma_master_set_state(TDMA_STATE_RUNNING);
                ESP_LOGI(TAG, "All %d node(s) registered — entering RUNNING state", count);
            }
        }
    }
}

// Unified ESP-NOW receive callback
static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (!info || !data || len <= 0) return;
    if (s_master_state != TDMA_STATE_IDLE) {
        master_handle_rx(info->src_addr, data, len);
    } else {
        slave_handle_rx(info->src_addr, data, len);
    }
}

esp_err_t esp_tdma_master_init(const esp_tdma_master_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_master_cfg = *cfg;
    esp_tdma_master_set_state(TDMA_STATE_REGISTERING);

    esp_now_register_recv_cb(espnow_recv_cb);

    esp_timer_create_args_t args = {
        .callback = master_beacon_timer_cb,
        .name     = "tdma_beacon"
    };
    return esp_timer_create(&args, &s_master_beacon_timer);
}

esp_err_t esp_tdma_master_start(void) {
    if (!s_master_beacon_timer) return ESP_ERR_INVALID_STATE;
    return esp_timer_start_periodic(s_master_beacon_timer,
                                    CONFIG_TDMA_BEACON_INTERVAL_MS * 1000ULL);
}

// ============================================================================
// SLAVE implementation
// ============================================================================

static esp_tdma_slave_cfg_t s_slave_cfg = {0};
static uint8_t  s_slave_node_id = 0;
static uint8_t  s_slave_gateway_mac[6] = {0};
static uint32_t s_slave_slot_offset_us = 0;
static uint32_t s_slave_packet_seq = 0;

static tdma_ringbuf_t s_slave_rb = {0};

static esp_timer_handle_t s_slave_slot_timer = NULL;
static TaskHandle_t       s_slave_tx_task_handle = NULL;

static bool     s_slave_reg_ack_pending = false;
static uint32_t s_slave_reg_ack_count   = 0;
static uint8_t  s_slave_reg_ack_buf[sizeof(tdma_reg_ack_t)] = {0};

static uint32_t s_slave_send_ok_count = 0;

static volatile uint8_t s_slave_afh_local_countdown = 0;
static uint8_t          s_slave_afh_next_channel     = 0;

esp_tdma_state_t esp_tdma_slave_get_state(void) {
    return s_slave_state;
}

void esp_tdma_slave_set_state(esp_tdma_state_t state) {
    s_slave_state = state;
    if (s_slave_cfg.on_state_changed) {
        s_slave_cfg.on_state_changed(state);
    }
}

uint32_t esp_tdma_slave_get_send_ok(void) {
    return s_slave_send_ok_count;
}

uint32_t esp_tdma_slave_get_queue_count(void) {
    return tdma_ringbuf_count(&s_slave_rb);
}

void esp_tdma_slave_set_config(uint8_t node_id, const uint8_t *gateway_mac,
                               const uint8_t *aes_lmk, uint32_t slot_offset_us) {
    s_slave_node_id       = node_id;
    memcpy(s_slave_gateway_mac, gateway_mac, 6);
    s_slave_slot_offset_us = slot_offset_us;

    if (esp_now_is_peer_exist(gateway_mac)) esp_now_del_peer(gateway_mac);
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, gateway_mac, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, aes_lmk, 16);
    esp_now_add_peer(&peer);

    // Build REG_ACK
    tdma_reg_ack_t ack = {
        .type            = TDMA_PKT_REG_ACK,
        .node_id         = node_id,
        .fw_needs_upgrade = false
    };
    uint32_t fw_ver = 20260622;
    memcpy(&ack.current_fw_ver, &fw_ver, sizeof(fw_ver));
    memcpy(s_slave_reg_ack_buf, &ack, sizeof(ack));

    s_slave_reg_ack_pending = true;
    s_slave_reg_ack_count   = 0;
    esp_tdma_slave_set_state(TDMA_STATE_REGISTERING);
    ESP_LOGI(TAG, "Slave configured: node_id=%d offset=%luus", node_id,
             (unsigned long)slot_offset_us);
}

esp_err_t esp_tdma_slave_enqueue(const void *data, size_t len) {
    if (!data) return ESP_ERR_INVALID_ARG;
    if (len > CONFIG_TDMA_PAYLOAD_SIZE) return ESP_ERR_INVALID_ARG;
    if (s_slave_state != TDMA_STATE_RUNNING) return ESP_ERR_INVALID_STATE;
    return tdma_ringbuf_push(&s_slave_rb, data, (uint8_t)len) ? ESP_OK : ESP_FAIL;
}

// Slot timer fires -> wake TX task
static void on_slot_timer_tick(void *arg) {
    if (s_slave_tx_task_handle) xTaskNotifyGive(s_slave_tx_task_handle);
}

static void trigger_my_slot(void) {
    if (s_slave_slot_offset_us > 0) {
        esp_timer_stop(s_slave_slot_timer);
        esp_timer_start_once(s_slave_slot_timer, s_slave_slot_offset_us);
    } else {
        if (s_slave_tx_task_handle) xTaskNotifyGive(s_slave_tx_task_handle);
    }
}

__attribute__((weak)) uint8_t battery_get_percentage(void) { return 100; }

// Slave TX task — sends one payload per beacon slot
static void tx_task(void *arg) {
    ESP_LOGI(TAG, "Slave TX task started");
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t current_seq = s_slave_packet_seq++;
        esp_tdma_state_t state = s_slave_state;

        if (current_seq % 100 == 1) {
            ESP_LOGW(TAG, "TX Task SLOT REACHED! Seq: %lu, State: %d, Offset: %lu",
                     (unsigned long)current_seq, (int)state,
                     (unsigned long)s_slave_slot_offset_us);
        }

        if (state == TDMA_STATE_REGISTERING && s_slave_reg_ack_pending) {
            esp_err_t err = esp_now_send(s_slave_gateway_mac,
                                         s_slave_reg_ack_buf,
                                         sizeof(tdma_reg_ack_t));
            if (err == ESP_OK) {
                s_slave_reg_ack_count++;
                if (s_slave_reg_ack_count >= 5) s_slave_reg_ack_pending = false;
            }
            continue;
        }

        if (state != TDMA_STATE_RUNNING) continue;

        tdma_payload_t slot_data;
        if (tdma_ringbuf_pop(&s_slave_rb, &slot_data) == 0) continue;

        tdma_data_pkt_t pkt = {0};
        pkt.type        = TDMA_PKT_DATA;
        pkt.node_id     = s_slave_node_id;
        pkt.battery_pct = battery_get_percentage();
        pkt.payload_len = slot_data.len;

        uint32_t seq = current_seq;
        memcpy(&pkt.packet_seq, &seq, sizeof(seq));
        memcpy(pkt.payload, slot_data.data, slot_data.len);

        esp_err_t err = esp_now_send(s_slave_gateway_mac,
                                     (uint8_t *)&pkt,
                                     offsetof(tdma_data_pkt_t, payload) + slot_data.len);
        if (err == ESP_OK) s_slave_send_ok_count++;
    }
}

// Slave RX handler — processes beacons from master
static void slave_handle_rx(const uint8_t *src_mac, const uint8_t *data, int len) {
    if (len < 1 || data[0] != TDMA_PKT_BEACON) return;
    if (len != sizeof(tdma_beacon_t)) return;

    const tdma_beacon_t *beacon = (const tdma_beacon_t *)data;

    // Channel-hop flywheel
    if (beacon->next_channel != 0 && beacon->switch_countdown > 0) {
        s_slave_afh_next_channel     = beacon->next_channel;
        s_slave_afh_local_countdown  = beacon->switch_countdown;
    }
    if (s_slave_afh_local_countdown > 0) {
        s_slave_afh_local_countdown--;
        if (s_slave_afh_local_countdown == 0 && s_slave_afh_next_channel != 0) {
            esp_wifi_set_channel(s_slave_afh_next_channel, WIFI_SECOND_CHAN_NONE);
            ESP_LOGW(TAG, "Slave: hopped to channel %d", s_slave_afh_next_channel);
            s_slave_afh_next_channel = 0;
        }
    }

    trigger_my_slot();
}

esp_err_t esp_tdma_slave_init(const esp_tdma_slave_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_slave_cfg = *cfg;

    tdma_ringbuf_init(&s_slave_rb, 64);

    esp_timer_create_args_t slot_args = {
        .callback  = on_slot_timer_tick,
        .name      = "tdma_slot"
    };
    esp_err_t err = esp_timer_create(&slot_args, &s_slave_slot_timer);
    if (err != ESP_OK) return err;

    xTaskCreatePinnedToCore(tx_task, "tdma_tx", 4096, NULL, 5,
                            &s_slave_tx_task_handle, 1);
    return ESP_OK;
}

esp_err_t esp_tdma_slave_start(void) {
    esp_now_register_recv_cb(espnow_recv_cb);
    return ESP_OK;
}
