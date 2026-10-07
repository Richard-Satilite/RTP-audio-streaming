#include "ac/ring_buffer.h"

#include <stdlib.h>
#include <string.h>

static size_t ac_ring_distance(size_t write_pos, size_t read_pos, size_t capacity)
{
    if (write_pos >= read_pos) return write_pos - read_pos;
    return capacity - read_pos + write_pos;
}

int ac_ring_buffer_init(ac_ring_buffer_t *ring, size_t capacity)
{
    if (ring == NULL || capacity < 2) return -1;
    memset(ring, 0, sizeof(*ring));
    ring->data = (uint8_t *)malloc(capacity);
    if (ring->data == NULL) return -1;
    ring->capacity = capacity;
    atomic_init(&ring->read_pos, 0);
    atomic_init(&ring->write_pos, 0);
    return 0;
}

void ac_ring_buffer_uninit(ac_ring_buffer_t *ring)
{
    if (ring == NULL) return;
    free(ring->data);
    memset(ring, 0, sizeof(*ring));
}

size_t ac_ring_buffer_available(const ac_ring_buffer_t *ring)
{
    size_t r, w;
    if (ring == NULL || ring->data == NULL) return 0;
    r = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
    w = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
    return ac_ring_distance(w, r, ring->capacity);
}

size_t ac_ring_buffer_free(const ac_ring_buffer_t *ring)
{
    if (ring == NULL || ring->capacity == 0) return 0;
    return (ring->capacity - 1) - ac_ring_buffer_available(ring);
}

size_t ac_ring_buffer_write(ac_ring_buffer_t *ring, const void *data, size_t len)
{
    size_t r, w, free_bytes, first;
    const uint8_t *src = (const uint8_t *)data;
    if (ring == NULL || ring->data == NULL || data == NULL || len == 0) return 0;
    r = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
    w = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
    free_bytes = (ring->capacity - 1) - ac_ring_distance(w, r, ring->capacity);
    if (len > free_bytes) len = free_bytes;
    first = ring->capacity - w;
    if (first > len) first = len;
    memcpy(ring->data + w, src, first);
    if (len > first) memcpy(ring->data, src + first, len - first);
    w = (w + len) % ring->capacity;
    atomic_store_explicit(&ring->write_pos, w, memory_order_release);
    return len;
}

size_t ac_ring_buffer_read(ac_ring_buffer_t *ring, void *data, size_t len)
{
    size_t r, w, available, first;
    uint8_t *dst = (uint8_t *)data;
    if (ring == NULL || ring->data == NULL || data == NULL || len == 0) return 0;
    r = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
    w = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
    available = ac_ring_distance(w, r, ring->capacity);
    if (len > available) len = available;
    first = ring->capacity - r;
    if (first > len) first = len;
    memcpy(dst, ring->data + r, first);
    if (len > first) memcpy(dst + first, ring->data, len - first);
    r = (r + len) % ring->capacity;
    atomic_store_explicit(&ring->read_pos, r, memory_order_release);
    return len;
}

void ac_ring_buffer_clear(ac_ring_buffer_t *ring)
{
    size_t w;
    if (ring == NULL) return;
    w = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
    atomic_store_explicit(&ring->read_pos, w, memory_order_release);
}
