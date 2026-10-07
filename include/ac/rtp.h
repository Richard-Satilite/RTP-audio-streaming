#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t pt; bool marker; uint16_t seq; uint32_t ts, ssrc;
    const uint8_t* payload; size_t payload_len;
} ac_rtp_packet_t;

size_t ac_rtp_write(uint8_t* buf, size_t cap, const ac_rtp_packet_t*);
int    ac_rtp_parse(const uint8_t* buf, size_t len, ac_rtp_packet_t* out);
