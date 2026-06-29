/**
 * @file esp_tdma_mac.h
 * @brief Public API for the ESP-TDMA-MAC component.
 *
 * The component provides a TDMA-scheduled, ESP-NOW-based MAC layer with:
 *  - Master (gateway): broadcasts a 10 ms beacon, receives node payloads,
 *    reports PER per node, and triggers Adaptive Frequency Hopping (AFH).
 *  - Slave (node): NFC-provisioned, fires its transmission in an assigned
 *    time slot, and enqueues arbitrary user payload bytes.
 *
 * The component is intentionally payload-agnostic. Application data
 * structures must be defined in the calling application, not here.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2024 SomatoSync Contributors
 */
#ifndef ESP_TDMA_MAC_H
#define ESP_TDMA_MAC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "tdma_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// State machine (Legacy & New)
// ============================================================================

/** Legacy TDMA engine states, shared by both master and slave sides. */
typedef enum {
    TDMA_STATE_IDLE,          /**< Not yet started. */
    TDMA_STATE_REGISTERING,   /**< Slave: waiting for master to acknowledge join. */
    TDMA_STATE_RUNNING,       /**< Normal operation. */
    TDMA_STATE_SILENT_ERROR,  /**< Fatal error — no recovery without reboot. */
    TDMA_STATE_OTA,           /**< OTA firmware update in progress. */
} esp_tdma_state_t;

/** New decoupled Link status states. */
typedef enum {
    ESP_TDMA_LINK_OFFLINE = 0,      /**< Synced beacon lost or searching for gateway. */
    ESP_TDMA_LINK_REGISTERING = 1,  /**< Node is registered or handshaking with gateway. */
    ESP_TDMA_LINK_CONNECTED = 2,    /**< Link established and slots are active. */
} esp_tdma_link_state_t;

/** New selectable SPSC queueing policies. */
typedef enum {
    ESP_TDMA_QUEUE_FIFO = 0,            /**< Standard First-In-First-Out. Fails/blocks if queue is full. */
    ESP_TDMA_QUEUE_DISCARD_STALE = 1,   /**< Discard older backlog frames on overflow, keeping only the newest frame. */
} esp_tdma_queue_policy_t;

/** Link state change callback. */
typedef void (*esp_tdma_link_state_cb_t)(esp_tdma_link_state_t new_state);

/** System state broadcast callback (for application-level states like OTA, RUNNING, etc.). */
typedef void (*esp_tdma_sys_state_cb_t)(uint8_t sys_state);

// ============================================================================
// Master (gateway) API
// ============================================================================

/**
 * @brief Master configuration passed to esp_tdma_master_init().
 */
typedef struct {
    /** Number of nodes expected before transitioning to TDMA_STATE_RUNNING. */
    uint8_t target_node_count;

    /**
     * @brief Called on every received data packet from any slave.
     */
    void (*on_data_received)(uint8_t node_id, uint32_t seq, uint8_t battery,
                             const void *payload, uint8_t payload_len);

    /**
     * @brief Called when a slave successfully completes the join handshake.
     */
    void (*on_node_registered)(uint8_t node_id, uint32_t fw_ver, bool upgrade_required);

    /**
     * @brief Legacy state changed callback (kept for backward compatibility).
     */
    void (*on_state_changed)(esp_tdma_state_t new_state);

    /**
     * @brief Decoupled system state change callback (triggered by system broadcasts).
     */
    esp_tdma_sys_state_cb_t on_sys_state_changed;

    /**
     * @brief If true, the component will skip initializing Wi-Fi and ESP-NOW.
     *        Use this if the application initializes Wi-Fi/ESP-NOW manually.
     */
    bool skip_wifi_init;
} esp_tdma_master_cfg_t;

/**
 * @brief Initialise the TDMA master engine.
 */
esp_err_t esp_tdma_master_init(const esp_tdma_master_cfg_t *cfg);

/**
 * @brief Start beacon broadcasting and begin accepting slave registrations.
 */
esp_err_t esp_tdma_master_start(void);

/**
 * @brief Pre-register a known node (from NFC or NVS) before it connects.
 */
void esp_tdma_master_register_node(uint8_t node_id, const uint8_t *mac_addr,
                                   const uint8_t *aes_lmk, uint32_t fw_ver,
                                   bool needs_upgrade);

/**
 * @brief Immediately schedule an Adaptive Frequency Hop to target_channel.
 */
void esp_tdma_master_trigger_afh(uint8_t target_channel);

/** @brief Return current master state. */
esp_tdma_state_t esp_tdma_master_get_state(void);

/** @brief Force the master into a given state (use with care). */
void esp_tdma_master_set_state(esp_tdma_state_t state);

// ============================================================================
// Slave (node) API
// ============================================================================

/**
 * @brief Slave configuration passed to esp_tdma_slave_init().
 */
typedef struct {
    /**
     * @brief Legacy state changed callback (kept for backward compatibility).
     */
    void (*on_state_changed)(esp_tdma_state_t new_state);

    /**
     * @brief New link sync state change callback.
     */
    esp_tdma_link_state_cb_t on_link_state_changed;

    /**
     * @brief Decoupled system state change callback (notified when master broadcasts a new state).
     */
    esp_tdma_sys_state_cb_t on_sys_state_changed;

    /**
     * @brief If true, the component will skip initializing Wi-Fi and ESP-NOW.
     *        Use this if the application initializes Wi-Fi/ESP-NOW manually.
     */
    bool skip_wifi_init;
} esp_tdma_slave_cfg_t;

/**
 * @brief Initialise the TDMA slave engine.
 */
esp_err_t esp_tdma_slave_init(const esp_tdma_slave_cfg_t *cfg);

/**
 * @brief Start listening for beacons and begin the join handshake.
 */
esp_err_t esp_tdma_slave_start(void);

/**
 * @brief Apply NFC-provisioned parameters and begin registration.
 */
void esp_tdma_slave_set_config(uint8_t node_id, const uint8_t *gateway_mac,
                               const uint8_t *aes_lmk, uint32_t slot_offset_us);

/**
 * @brief Enqueue a user payload for transmission in the next TDMA slot (legacy API).
 *
 * Internally maps to esp_tdma_slave_enqueue_with_policy() using ESP_TDMA_QUEUE_DISCARD_STALE.
 */
esp_err_t esp_tdma_slave_enqueue(const void *data, size_t len);

/**
 * @brief Enqueue a user payload with a configurable queuing policy.
 *
 * Thread-safe (lock-free SPSC ring buffer). May be called from any task.
 *
 * @param data   Pointer to the payload buffer.
 * @param len    Number of bytes to send. Must be <= CONFIG_TDMA_PAYLOAD_SIZE.
 * @param policy Queuing policy (FIFO or DISCARD_STALE).
 */
esp_err_t esp_tdma_slave_enqueue_with_policy(const void *data, size_t len, esp_tdma_queue_policy_t policy);

/** @brief Return current slave state. */
esp_tdma_state_t esp_tdma_slave_get_state(void);

/** @brief Force the slave into a given state (use with care). */
void esp_tdma_slave_set_state(esp_tdma_state_t state);

/** @brief Return total number of packets successfully sent since boot. */
uint32_t esp_tdma_slave_get_send_ok(void);
uint32_t esp_tdma_slave_get_queue_count(void);

// ============================================================================
// Internal ring buffer
// ============================================================================

/** One entry in the TDMA payload ring buffer. */
typedef struct {
    uint8_t data[CONFIG_TDMA_PAYLOAD_SIZE]; /**< Raw user payload bytes. */
    uint8_t len;                            /**< Valid bytes in data[]. */
} tdma_payload_t;

/** Lock-free single-producer / single-consumer ring buffer for tdma_payload_t. */
typedef struct {
    tdma_payload_t  *buffer;   /**< Heap-allocated buffer (capacity entries). */
    uint32_t         capacity; /**< Must be a power of two. */
    volatile uint32_t head;    /**< Write index (producer). */
    volatile uint32_t tail;    /**< Read index (consumer). */
} tdma_ringbuf_t;

void     tdma_ringbuf_init(tdma_ringbuf_t *rb, uint32_t capacity);
bool     tdma_ringbuf_push(tdma_ringbuf_t *rb, const void *data, uint8_t len);
uint32_t tdma_ringbuf_pop(tdma_ringbuf_t *rb, tdma_payload_t *out);
uint32_t tdma_ringbuf_count(const tdma_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* ESP_TDMA_MAC_H */
