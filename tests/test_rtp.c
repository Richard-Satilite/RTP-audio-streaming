#include "ac/rtp.h"
#include "ac/common.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t payload[] = {1, 2, 3, 4, 5};
    uint8_t packet[64];
    ac_rtp_packet_t in;
    ac_rtp_packet_t out;
    size_t len;

    memset(&in, 0, sizeof(in));
    in.pt = AC_PT_L16;
    in.marker = true;
    in.seq = 65530;
    in.ts = 0x12345678u;
    in.ssrc = 0xdeadbeefu;
    in.payload = payload;
    in.payload_len = sizeof(payload);

    len = ac_rtp_write(packet, sizeof(packet), &in);
    assert(len == 12 + sizeof(payload));
    assert(ac_rtp_parse(packet, len, &out) == 0);
    assert(out.pt == in.pt);
    assert(out.marker == in.marker);
    assert(out.seq == in.seq);
    assert(out.ts == in.ts);
    assert(out.ssrc == in.ssrc);
    assert(out.payload_len == sizeof(payload));
    assert(memcmp(out.payload, payload, sizeof(payload)) == 0);
    puts("test_rtp: OK");
    return 0;
}
