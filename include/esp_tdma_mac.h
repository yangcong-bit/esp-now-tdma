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
// State machine
// ============================================================================

/** TDMA engine states, shared by both master and slave sides. */
typedef enum {
    TDMA_STATE_IDLE,          /**< Not yet started. */
    TDMA_STATE_REGISTERING,   /**< Slave: waiting for master to acknowledge join. */
    TDMA_STATE_RUNNING,       /**< Normal operation. */
    TDMA_STATE_SILENT_ERROR,  /**< Fatal error — no recovery without reboot. */
    TDMA_STATE_OTA,           /**< OTA firmware update in progress. */
} esp_tdma_state_t;

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
     *
     * @param node_id     Node that sent the packet (1-based).
     * @param seq         Packet sequence number.
     * @param battery     Node battery percentage (0-100).
     * @param payload     Pointer to the raw user payload bytes.
     * @param payload_len Number of valid bytes in payload.
     *
     * @note This callback is invoked from the ESP-NOW RX callback context
     *       (a high-priority Wi-Fi task). Keep it short; defer heavy work.
     */
    void (*on_data_received)(uint8_t node_id, uint32_t seq, uint8_t battery,
                             const void *payload, uint8_t payload_len);

    /**
     * @brief Called when a slave successfully completes the join handshake.
     *
     * @param node_id          Registered node ID.
     * @param fw_ver           Slave's current firmware version.
     * @param upgrade_required True if slave firmware is older than required.
     */
    void (*on_node_registered)(uint8_t node_id, uint32_t fw_ver, bool upgrade_required);

    /**
     * @brief Called whenever the master engine changes state.
     *
     * @param new_state The new TDMA state.
     */
    void (*on_state_changed)(esp_tdma_state_t new_state);
} esp_tdma_master_cfg_t;

/**
 * @brief Initialise the TDMA master engine.
 * @param cfg  Non-NULL pointer to master configuration.
 * @return ESP_OK on success.
 */
esp_err_t esp_tdma_master_init(const esp_tdma_master_cfg_t *cfg);

/**
 * @brief Start beacon broadcasting and begin accepting slave registrations.
 * @return ESP_OK on success.
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
 * @param target_channel  Wi-Fi channel (1, 6, or 11 recommended).
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
     * @brief Called whenever the slave engine changes state.
     * @param new_state The new TDMA state.
     */
    void (*on_state_changed)(esp_tdma_state_t new_state);
} esp_tdma_slave_cfg_t;

/**
 * @brief Initialise the TDMA slave engine.
 * @param cfg  Non-NULL pointer to slave configuration.
 * @return ESP_OK on success.
 */
esp_err_t esp_tdma_slave_init(const esp_tdma_slave_cfg_t *cfg);

/**
 * @brief Start listening for beacons and begin the join handshake.
 * @return ESP_OK on success.
 */
esp_err_t esp_tdma_slave_start(void);

/**
 * @brief Apply NFC-provisioned parameters and begin registration.
 *
 * @param node_id        Assigned node ID.
 * @param gateway_mac    Gateway ESP-NOW MAC (6 bytes).
 * @param aes_lmk        AES-128 Local Master Key (16 bytes).
 * @param slot_offset_us TX slot offset within the beacon period (µs).
 */
void esp_tdma_slave_set_config(uint8_t node_id, const uint8_t *gateway_mac,
                               const uint8_t *aes_lmk, uint32_t slot_offset_us);

/**
 * @brief Enqueue a user payload for transmission in the next TDMA slot.
 *
 * Thread-safe (lock-free SPSC ring buffer). May be called from any task.
 *
 * @param data Pointer to the payload buffer.
 * @param len  Number of bytes to send. Must be <= CONFIG_TDMA_PAYLOAD_SIZE.
 * @return ESP_OK if enqueued, ESP_FAIL if the ring buffer is full,
 *         ESP_ERR_INVALID_ARG if len > CONFIG_TDMA_PAYLOAD_SIZE,
 *         ESP_ERR_INVALID_STATE if the slave is not in TDMA_STATE_RUNNING.
 */
esp_err_t esp_tdma_slave_enqueue(const void *data, size_t len);

/** @brief Return current slave state. */
esp_tdma_state_t esp_tdma_slave_get_state(void);

/** @brief Force the slave into a given state (use with care). */
void esp_tdma_slave_set_state(esp_tdma_state_t state);

/** @brief Return total number of packets successfully sent since boot. */
uint32_t esp_tdma_slave_get_send_ok(void);
uint32_t esp_tdma_slave_get_queue_count(void);

// ============================================================================
// Internal ring buffer (exposed for advanced users; normally not needed)
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
