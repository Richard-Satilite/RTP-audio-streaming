#pragma once

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t capacity;
    _Atomic size_t read_pos;
    _Atomic size_t write_pos;
} ac_ring_buffer_t;

int ac_ring_buffer_init(ac_ring_buffer_t *ring, size_t capacity);
void ac_ring_buffer_uninit(ac_ring_buffer_t *ring);
size_t ac_ring_buffer_write(ac_ring_buffer_t *ring, const void *data, size_t len);
size_t ac_ring_buffer_read(ac_ring_buffer_t *ring, void *data, size_t len);
size_t ac_ring_buffer_available(const ac_ring_buffer_t *ring);
size_t ac_ring_buffer_free(const ac_ring_buffer_t *ring);
void ac_ring_buffer_clear(ac_ring_buffer_t *ring);
