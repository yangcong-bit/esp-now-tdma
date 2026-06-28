/**
 * @file tdma_protocol.h
 * @brief ESP-TDMA-MAC on-air packet definitions (transport layer only).
 *
 * This header defines the wire format for all packets exchanged between the
 * TDMA master (gateway) and TDMA slaves (nodes). It is intentionally
 * payload-agnostic: the user-supplied payload bytes are carried inside
 * tdma_data_pkt_t::payload without any interpretation by the component.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2024 SomatoSync Contributors
 */
#ifndef TDMA_PROTOCOL_H
#define TDMA_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/** Maximum user payload bytes per data packet (must match CONFIG_TDMA_PAYLOAD_SIZE). */
#ifndef CONFIG_TDMA_PAYLOAD_SIZE
#define CONFIG_TDMA_PAYLOAD_SIZE 220
#endif

#pragma pack(push, 1)

// ============================================================================
// Part 1: NFC OOB Physical-Layer Provisioning (ST25DV Mailbox)
// ============================================================================

/** Magic word written at the start of every NFC provisioning payload. */
#define TDMA_NFC_MAGIC 0xAA55

/**
 * @brief NFC provisioning payload written by the gateway into the ST25DV
 *        Mailbox. Total: 38 bytes.
 *
 * The slave reads this payload after an NFC tap to learn its node ID,
 * gateway MAC, AES-128 LMK, and time-slot offset.
 */
typedef struct {
    uint16_t magic_word;      /**< Must equal TDMA_NFC_MAGIC (0xAA55). */
    uint8_t  cmd_type;        /**< 0x01 = join & overwrite slot, 0x02 = wake only. */
    uint8_t  node_id;         /**< Assigned topology node ID (1-based). */
    uint8_t  gateway_mac[6];  /**< Gateway ESP-NOW MAC address. */
    uint8_t  aes_lmk[16];     /**< AES-128 Local Master Key for ESP-NOW encryption. */
    uint32_t slot_offset_us;  /**< TX slot offset within the 10 ms TDMA frame (µs). */
    uint32_t target_fw_ver;   /**< Required firmware version (for OTA checks). */
    uint32_t crc32;           /**< CRC-32 of the preceding 34 bytes. */
} tdma_nfc_payload_t; /* 38 bytes */

// ============================================================================
// Part 2: ESP-NOW 2.4 GHz RF Business Protocol
// ============================================================================

/** First byte of every ESP-NOW packet; identifies the packet type. */
typedef enum {
    TDMA_PKT_BEACON   = 0xFF, /**< Master → broadcast: TDMA clock alignment beacon. */
    TDMA_PKT_DATA     = 0x01, /**< Slave  → master:    user payload container. */
    TDMA_PKT_REG_ACK  = 0x02, /**< Slave  → master:    join handshake acknowledgement. */
    TDMA_PKT_CMD_OTA  = 0x03, /**< Master → broadcast: enter OTA mode command. */
    TDMA_PKT_LOG      = 0x04, /**< Slave  → master:    wireless diagnostic log line. */
} tdma_pkt_type_t;

// ---------------------------------------------------------------------------
// [Packet 0xFF] Master beacon — synchronisation gun
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;                 /**< TDMA_PKT_BEACON (0xFF). */
    uint32_t frame_seq;            /**< Global scheduling frame sequence number. */
    uint64_t global_time_us;       /**< Master absolute time in microseconds. */
    uint16_t active_nodes_bitmask; /**< Bitmask of currently alive nodes. */
    uint8_t  sys_state;            /**< System state: 0=normal, 1=silent, 2=OTA. */
    uint8_t  next_channel;         /**< AFH target channel (0 = no hop pending). */
    uint8_t  switch_countdown;     /**< Frames remaining before channel switch. */
} tdma_beacon_t;

// ---------------------------------------------------------------------------
// [Packet 0x02] Slave registration acknowledgement
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;             /**< TDMA_PKT_REG_ACK (0x02). */
    uint8_t  node_id;          /**< Node ID confirming registration. */
    uint32_t current_fw_ver;   /**< Node's current firmware version. */
    bool     fw_needs_upgrade; /**< True if current_fw_ver < target_fw_ver. */
} tdma_reg_ack_t;

// ---------------------------------------------------------------------------
// [Packet 0x04] Slave diagnostic log
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;         /**< TDMA_PKT_LOG (0x04). */
    uint8_t  node_id;      /**< Originating node ID. */
    uint8_t  log_level;    /**< Log severity (mirrors ESP_LOG_* values). */
    char     log_msg[230]; /**< Null-terminated log string. */
} tdma_log_pkt_t;

// ---------------------------------------------------------------------------
// [Packet 0x01] Slave data packet — user payload container
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;                              /**< TDMA_PKT_DATA (0x01). */
    uint8_t  node_id;                           /**< Originating node ID. */
    uint8_t  battery_pct;                       /**< Node battery percentage (0-100). */
    uint32_t packet_seq;                        /**< Monotonically increasing sequence number. */
    uint8_t  payload_len;                       /**< Valid bytes in payload[]. */
    uint8_t  payload[CONFIG_TDMA_PAYLOAD_SIZE]; /**< User-defined payload bytes. */
} tdma_data_pkt_t;

#pragma pack(pop)

#endif /* TDMA_PROTOCOL_H */
