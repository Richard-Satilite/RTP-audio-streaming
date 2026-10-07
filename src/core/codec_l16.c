#include "ac/codec.h"

size_t ac_codec_frames_for_duration(int frame_ms_x10)
{
    if (frame_ms_x10 <= 0) {
        return 0;
    }

    /*
     * frame_ms_x10 está em décimos de ms.
     *
     * frames = sample_rate * ms / 1000
     *        = sample_rate * frame_ms_x10 / 10000
     */
    return ((size_t)AC_SAMPLE_RATE * (size_t)frame_ms_x10) / 10000u;
}

size_t ac_l16_payload_size(size_t frames)
{
    return frames * AC_CHANNELS * sizeof(int16_t);
}

size_t ac_l16_encode(const int16_t *pcm,
                     size_t frames,
                     uint8_t *out,
                     size_t out_cap)
{
    size_t samples;
    size_t needed;

    if (pcm == NULL || out == NULL) {
        return 0;
    }

    samples = frames * AC_CHANNELS;
    needed = samples * sizeof(int16_t);

    if (out_cap < needed) {
        return 0;
    }

    for (size_t i = 0; i < samples; ++i) {
        uint16_t sample = (uint16_t)pcm[i];

        out[i * 2 + 0] = (uint8_t)((sample >> 8) & 0xff);
        out[i * 2 + 1] = (uint8_t)(sample & 0xff);
    }

    return needed;
}

size_t ac_l16_decode(const uint8_t *payload,
                     size_t payload_len,
                     int16_t *pcm,
                     size_t pcm_cap)
{
    size_t samples;
    size_t frames;

    if (payload == NULL || pcm == NULL) {
        return 0;
    }

    if ((payload_len % sizeof(int16_t)) != 0) {
        return 0;
    }

    samples = payload_len / sizeof(int16_t);

    if ((samples % AC_CHANNELS) != 0) {
        return 0;
    }

    if (pcm_cap < samples) {
        return 0;
    }

    for (size_t i = 0; i < samples; ++i) {
        uint16_t hi = payload[i * 2 + 0];
        uint16_t lo = payload[i * 2 + 1];
        uint16_t sample = (uint16_t)((hi << 8) | lo);

        pcm[i] = (int16_t)sample;
    }

    frames = samples / AC_CHANNELS;
    return frames;
}