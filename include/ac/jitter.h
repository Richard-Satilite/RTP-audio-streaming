#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ac/common.h"
#include "ac/rtp.h"

#define AC_JITTER_MAX_PACKETS 128
#define AC_JITTER_PAYLOAD_CAP (AC_MAX_PKT - 12)

typedef enum {
    AC_JITTER_POP_NONE = 0,
    AC_JITTER_POP_PACKET = 1,
    AC_JITTER_POP_LOST = 2
} ac_jitter_pop_result_t;

typedef struct {
    uint64_t packets_received;
    uint64_t packets_accepted;
    uint64_t packets_duplicate;
    uint64_t packets_late;
    uint64_t packets_malformed;
    uint64_t packets_lost;
    uint64_t packets_reordered;
    uint64_t packets_concealed;
    uint64_t bytes_received;
    uint64_t bytes_delivered;
    double jitter_ms;
    size_t buffered_packets;
} ac_jitter_stats_t;

typedef struct {
    int used;
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
    size_t payload_len;
    uint8_t payload[AC_JITTER_PAYLOAD_CAP];
} ac_jitter_slot_t;

typedef struct {
    ac_jitter_slot_t slots[AC_JITTER_MAX_PACKETS];
    size_t capacity;
    size_t prebuffer_packets;
    size_t reorder_packets;
    int started;
    uint16_t next_seq;
    uint32_t next_ts;
    uint32_t ssrc;
    uint32_t sample_rate;
    uint32_t frames_per_packet;
    uint64_t last_arrival_us;
    uint32_t last_rtp_ts;
    int have_highest_seq;
    uint16_t highest_seq;
    double jitter_frames;
    ac_jitter_stats_t stats;
} ac_jitter_t;

int ac_jitter_init(ac_jitter_t *jitter,
                   size_t capacity_packets,
                   size_t prebuffer_packets,
                   size_t reorder_packets,
                   uint32_t sample_rate,
                   uint32_t frames_per_packet);
void ac_jitter_reset(ac_jitter_t *jitter);

int ac_jitter_push(ac_jitter_t *jitter,
                   const ac_rtp_packet_t *packet,
                   uint64_t arrival_us);

ac_jitter_pop_result_t ac_jitter_pop(ac_jitter_t *jitter,
                                     uint8_t *payload,
                                     size_t payload_cap,
                                     size_t *payload_len,
                                     uint16_t *seq,
                                     uint32_t *ts,
                                     uint32_t *ssrc);

size_t ac_jitter_buffered_packets(const ac_jitter_t *jitter);
void ac_jitter_get_stats(const ac_jitter_t *jitter, ac_jitter_stats_t *stats);
