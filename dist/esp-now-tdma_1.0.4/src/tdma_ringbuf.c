/**
 * @file tdma_ringbuf.c
 * @brief Lock-free SPSC ring buffer for tdma_payload_t entries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_tdma_mac.h"
#include <string.h>
#include <stdlib.h>

void tdma_ringbuf_init(tdma_ringbuf_t *rb, uint32_t capacity) {
    rb->capacity = capacity;
    rb->buffer   = malloc(capacity * sizeof(tdma_payload_t));
    if (rb->buffer) {
        memset(rb->buffer, 0, capacity * sizeof(tdma_payload_t));
    }
    rb->head = 0;
    rb->tail = 0;
}

bool tdma_ringbuf_push(tdma_ringbuf_t *rb, const void *data, uint8_t len) {
    if (!rb->buffer || len > CONFIG_TDMA_PAYLOAD_SIZE) return false;

    uint32_t head = __atomic_load_n(&rb->head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1) & (rb->capacity - 1);

    if (next == tail) return false; /* Buffer full */

    memcpy(rb->buffer[head].data, data, len);
    rb->buffer[head].len = len;

    __atomic_store_n(&rb->head, next, __ATOMIC_RELEASE);
    return true;
}

uint32_t tdma_ringbuf_pop(tdma_ringbuf_t *rb, tdma_payload_t *out) {
    if (!rb->buffer) return 0;

    uint32_t tail = __atomic_load_n(&rb->tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);

    if (tail == head) return 0; /* Buffer empty */

    *out = rb->buffer[tail];
    uint32_t next = (tail + 1) & (rb->capacity - 1);
    __atomic_store_n(&rb->tail, next, __ATOMIC_RELEASE);
    return 1;
}

uint32_t tdma_ringbuf_count(const tdma_ringbuf_t *rb) {
    uint32_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);
    return (head - tail) & (rb->capacity - 1);
}
