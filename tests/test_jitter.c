#include "ac/jitter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static ac_rtp_packet_t make_packet(uint16_t seq, uint32_t ts, uint8_t value)
{
    static uint8_t payloads[8][4];
    ac_rtp_packet_t packet;
    payloads[seq % 8][0] = value;
    payloads[seq % 8][1] = value + 1;
    payloads[seq % 8][2] = value + 2;
    payloads[seq % 8][3] = value + 3;
    memset(&packet, 0, sizeof(packet));
    packet.pt = AC_PT_L16;
    packet.seq = seq;
    packet.ts = ts;
    packet.ssrc = 42;
    packet.payload = payloads[seq % 8];
    packet.payload_len = 4;
    return packet;
}

int main(void)
{
    ac_jitter_t jitter;
    uint8_t out[32];
    size_t len;
    uint16_t seq;
    uint32_t ts, ssrc;
    ac_jitter_pop_result_t result;
    ac_jitter_stats_t stats;
    ac_rtp_packet_t p;

    assert(ac_jitter_init(&jitter, 16, 3, 2, 48000, 120) == 0);
    p = make_packet(100, 0, 1); assert(ac_jitter_push(&jitter, &p, 0) == 0);
    p = make_packet(102, 240, 3); assert(ac_jitter_push(&jitter, &p, 5000) == 0);
    p = make_packet(101, 120, 2); assert(ac_jitter_push(&jitter, &p, 7000) == 0);

    result = ac_jitter_pop(&jitter, out, sizeof(out), &len, &seq, &ts, &ssrc);
    assert(result == AC_JITTER_POP_PACKET && seq == 100);
    result = ac_jitter_pop(&jitter, out, sizeof(out), &len, &seq, &ts, &ssrc);
    assert(result == AC_JITTER_POP_PACKET && seq == 101);
    result = ac_jitter_pop(&jitter, out, sizeof(out), &len, &seq, &ts, &ssrc);
    assert(result == AC_JITTER_POP_PACKET && seq == 102);

    p = make_packet(104, 480, 5); assert(ac_jitter_push(&jitter, &p, 12000) == 0);
    p = make_packet(105, 600, 6); assert(ac_jitter_push(&jitter, &p, 15000) == 0);
    p = make_packet(106, 720, 7); assert(ac_jitter_push(&jitter, &p, 18000) == 0);
    result = ac_jitter_pop(&jitter, out, sizeof(out), &len, &seq, &ts, &ssrc);
    assert(result == AC_JITTER_POP_LOST && seq == 103);

    ac_jitter_get_stats(&jitter, &stats);
    assert(stats.packets_lost >= 1);
    assert(stats.jitter_ms >= 0.0);
    puts("test_jitter: OK");
    return 0;
}
