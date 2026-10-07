#include "ac/jitter.h"

#include <math.h>
#include <string.h>

static int ac_seq_before(uint16_t a, uint16_t b)
{
    return (int16_t)(a - b) < 0;
}

static int32_t ac_seq_distance(uint16_t a, uint16_t b)
{
    return (int16_t)(a - b);
}

static size_t ac_jitter_find_slot(const ac_jitter_t *jitter, uint16_t seq)
{
    size_t i;
    for (i = 0; i < jitter->capacity; ++i) {
        if (jitter->slots[i].used && jitter->slots[i].seq == seq) return i;
    }
    return jitter->capacity;
}

static size_t ac_jitter_find_free_slot(const ac_jitter_t *jitter)
{
    size_t i;
    for (i = 0; i < jitter->capacity; ++i) {
        if (!jitter->slots[i].used) return i;
    }
    return jitter->capacity;
}

static size_t ac_jitter_find_earliest(const ac_jitter_t *jitter)
{
    size_t i;
    size_t best = jitter->capacity;
    for (i = 0; i < jitter->capacity; ++i) {
        if (!jitter->slots[i].used) continue;
        if (best == jitter->capacity ||
            ac_seq_before(jitter->slots[i].seq, jitter->slots[best].seq)) {
            best = i;
        }
    }
    return best;
}

static size_t ac_jitter_count(const ac_jitter_t *jitter)
{
    size_t i, count = 0;
    for (i = 0; i < jitter->capacity; ++i) count += jitter->slots[i].used ? 1u : 0u;
    return count;
}

int ac_jitter_init(ac_jitter_t *jitter,
                   size_t capacity_packets,
                   size_t prebuffer_packets,
                   size_t reorder_packets,
                   uint32_t sample_rate,
                   uint32_t frames_per_packet)
{
    if (jitter == NULL || capacity_packets < 4 ||
        capacity_packets > AC_JITTER_MAX_PACKETS ||
        prebuffer_packets == 0 || prebuffer_packets >= capacity_packets ||
        reorder_packets == 0 || reorder_packets >= capacity_packets ||
        sample_rate == 0 || frames_per_packet == 0) return -1;
    memset(jitter, 0, sizeof(*jitter));
    jitter->capacity = capacity_packets;
    jitter->prebuffer_packets = prebuffer_packets;
    jitter->reorder_packets = reorder_packets;
    jitter->sample_rate = sample_rate;
    jitter->frames_per_packet = frames_per_packet;
    return 0;
}

void ac_jitter_reset(ac_jitter_t *jitter)
{
    if (jitter == NULL) return;
    memset(jitter->slots, 0, sizeof(jitter->slots));
    jitter->started = 0;
    jitter->next_seq = 0;
    jitter->next_ts = 0;
    jitter->ssrc = 0;
    jitter->last_arrival_us = 0;
    jitter->last_rtp_ts = 0;
    jitter->have_highest_seq = 0;
    jitter->highest_seq = 0;
    jitter->jitter_frames = 0.0;
    memset(&jitter->stats, 0, sizeof(jitter->stats));
}

int ac_jitter_push(ac_jitter_t *jitter,
                   const ac_rtp_packet_t *packet,
                   uint64_t arrival_us)
{
    size_t slot_index;
    size_t buffered;
    if (jitter == NULL || packet == NULL || packet->payload == NULL ||
        packet->payload_len == 0 || packet->payload_len > AC_JITTER_PAYLOAD_CAP) return -1;

    jitter->stats.packets_received++;
    jitter->stats.bytes_received += packet->payload_len;

    if (jitter->ssrc != 0 && packet->ssrc != jitter->ssrc) return -2;
    if (jitter->ssrc == 0) jitter->ssrc = packet->ssrc;

    if (jitter->last_arrival_us != 0) {
        uint64_t arrival_delta = arrival_us - jitter->last_arrival_us;
        uint32_t rtp_delta = packet->ts - jitter->last_rtp_ts;
        double arrival_frames = ((double)arrival_delta * (double)jitter->sample_rate) / 1000000.0;
        double d = arrival_frames - (double)rtp_delta;
        double abs_d = fabs(d);
        jitter->jitter_frames += (abs_d - jitter->jitter_frames) / 16.0;
    }
    jitter->last_arrival_us = arrival_us;
    jitter->last_rtp_ts = packet->ts;
    jitter->stats.jitter_ms = (jitter->jitter_frames * 1000.0) / (double)jitter->sample_rate;

    if (jitter->started && ac_seq_before(packet->seq, jitter->next_seq)) {
        jitter->stats.packets_late++;
        return 1;
    }

    if (jitter->have_highest_seq && ac_seq_before(packet->seq, jitter->highest_seq)) {
        jitter->stats.packets_reordered++;
    } else if (!jitter->have_highest_seq || ac_seq_before(jitter->highest_seq, packet->seq)) {
        jitter->highest_seq = packet->seq;
        jitter->have_highest_seq = 1;
    }

    if (ac_jitter_find_slot(jitter, packet->seq) != jitter->capacity) {
        jitter->stats.packets_duplicate++;
        return 1;
    }

    slot_index = ac_jitter_find_free_slot(jitter);
    if (slot_index == jitter->capacity) {
        /* Keep the newest packet if the buffer is full. Drop the oldest buffered packet. */
        slot_index = ac_jitter_find_earliest(jitter);
        if (slot_index == jitter->capacity) return -1;
        jitter->slots[slot_index].used = 0;
        jitter->stats.packets_late++;
    }

    jitter->slots[slot_index].used = 1;
    jitter->slots[slot_index].seq = packet->seq;
    jitter->slots[slot_index].ts = packet->ts;
    jitter->slots[slot_index].ssrc = packet->ssrc;
    jitter->slots[slot_index].payload_len = packet->payload_len;
    memcpy(jitter->slots[slot_index].payload, packet->payload, packet->payload_len);
    jitter->stats.packets_accepted++;

    buffered = ac_jitter_count(jitter);
    jitter->stats.buffered_packets = buffered;
    return 0;
}

ac_jitter_pop_result_t ac_jitter_pop(ac_jitter_t *jitter,
                                     uint8_t *payload,
                                     size_t payload_cap,
                                     size_t *payload_len,
                                     uint16_t *seq,
                                     uint32_t *ts,
                                     uint32_t *ssrc)
{
    size_t slot_index;
    size_t i;
    int32_t max_ahead = -1;

    if (jitter == NULL || payload == NULL || payload_len == NULL ||
        seq == NULL || ts == NULL || ssrc == NULL) return AC_JITTER_POP_NONE;

    if (!jitter->started) {
        if (ac_jitter_count(jitter) < jitter->prebuffer_packets) return AC_JITTER_POP_NONE;
        slot_index = ac_jitter_find_earliest(jitter);
        if (slot_index == jitter->capacity) return AC_JITTER_POP_NONE;
        jitter->next_seq = jitter->slots[slot_index].seq;
        jitter->next_ts = jitter->slots[slot_index].ts;
        jitter->started = 1;
    }

    slot_index = ac_jitter_find_slot(jitter, jitter->next_seq);
    if (slot_index != jitter->capacity) {
        ac_jitter_slot_t *slot = &jitter->slots[slot_index];
        if (slot->payload_len > payload_cap) return AC_JITTER_POP_NONE;
        memcpy(payload, slot->payload, slot->payload_len);
        *payload_len = slot->payload_len;
        *seq = slot->seq;
        *ts = slot->ts;
        *ssrc = slot->ssrc;
        jitter->next_seq = (uint16_t)(slot->seq + 1u);
        jitter->next_ts = slot->ts + jitter->frames_per_packet;
        slot->used = 0;
        jitter->stats.bytes_delivered += *payload_len;
        jitter->stats.buffered_packets = ac_jitter_count(jitter);
        return AC_JITTER_POP_PACKET;
    }

    for (i = 0; i < jitter->capacity; ++i) {
        if (!jitter->slots[i].used) continue;
        {
            int32_t distance = ac_seq_distance(jitter->slots[i].seq, jitter->next_seq);
            if (distance > max_ahead) max_ahead = distance;
        }
    }

    if (max_ahead >= (int32_t)jitter->reorder_packets) {
        jitter->stats.packets_lost++;
        jitter->stats.packets_concealed++;
        *payload_len = 0;
        *seq = jitter->next_seq;
        *ts = jitter->next_ts;
        *ssrc = jitter->ssrc;
        jitter->next_seq++;
        jitter->next_ts += jitter->frames_per_packet;
        return AC_JITTER_POP_LOST;
    }

    return AC_JITTER_POP_NONE;
}

size_t ac_jitter_buffered_packets(const ac_jitter_t *jitter)
{
    if (jitter == NULL) return 0;
    return ac_jitter_count(jitter);
}

void ac_jitter_get_stats(const ac_jitter_t *jitter, ac_jitter_stats_t *stats)
{
    if (jitter == NULL || stats == NULL) return;
    *stats = jitter->stats;
    stats->buffered_packets = ac_jitter_count(jitter);
}
