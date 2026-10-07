#include "ac/codec.h"
#include "ac/common.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    int16_t pcm[AC_CHANNELS * 240];
    int16_t decoded[AC_CHANNELS * 240];
    uint8_t payload[AC_MAX_PKT];
    size_t i;
    size_t bytes;
    size_t frames;

    for (i = 0; i < sizeof(pcm) / sizeof(pcm[0]); ++i) {
        pcm[i] = (int16_t)((i * 997u) ^ 0x1234u);
    }
    bytes = ac_l16_encode(pcm, 240, payload, sizeof(payload));
    assert(bytes == sizeof(pcm));
    frames = ac_l16_decode(payload, bytes, decoded, sizeof(decoded) / sizeof(decoded[0]));
    assert(frames == 240);
    assert(memcmp(pcm, decoded, sizeof(pcm)) == 0);
    assert(ac_codec_frames_for_duration(50) == 240);
    assert(ac_codec_frames_for_duration(25) == 120);
    puts("test_codec_l16: OK");
    return 0;
}
