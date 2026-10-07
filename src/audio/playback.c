#include "ac/playback.h"

#include <stdlib.h>
#include <string.h>

#include "miniaudio.h"

struct ac_playback {
    ma_context context;
    ma_device device;
    ac_ring_buffer_t *ring;
    uint32_t channels;
};

static void ac_playback_callback(ma_device *device,
                                 void *output,
                                 const void *input,
                                 ma_uint32 frame_count)
{
    ac_playback_t *playback = (ac_playback_t *)device->pUserData;
    size_t bytes;
    size_t read;
    (void)input;
    if (playback == NULL || output == NULL) return;
    bytes = (size_t)frame_count * playback->channels * sizeof(int16_t);
    read = ac_ring_buffer_read(playback->ring, output, bytes);
    if (read < bytes) memset((uint8_t *)output + read, 0, bytes - read);
}

int ac_playback_open(ac_playback_t **playback, const ac_playback_config_t *config)
{
    ac_playback_t *p;
    ma_device_config device_config;
    if (playback == NULL || config == NULL || config->ring == NULL ||
        config->sample_rate == 0 || config->channels == 0) return -1;
    *playback = NULL;
    p = (ac_playback_t *)calloc(1, sizeof(*p));
    if (p == NULL) return -1;
    p->ring = config->ring;
    p->channels = config->channels;
    if (ma_context_init(NULL, 0, NULL, &p->context) != MA_SUCCESS) {
        free(p);
        return -1;
    }
    device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_s16;
    device_config.playback.channels = config->channels;
    device_config.sampleRate = config->sample_rate;
    device_config.dataCallback = ac_playback_callback;
    device_config.pUserData = p;
    if (ma_device_init(&p->context, &device_config, &p->device) != MA_SUCCESS) {
        ma_context_uninit(&p->context);
        free(p);
        return -1;
    }
    *playback = p;
    return 0;
}

int ac_playback_start(ac_playback_t *playback)
{
    if (playback == NULL) return -1;
    return ma_device_start(&playback->device) == MA_SUCCESS ? 0 : -1;
}

void ac_playback_stop(ac_playback_t *playback)
{
    if (playback == NULL) return;
    (void)ma_device_stop(&playback->device);
}

void ac_playback_close(ac_playback_t *playback)
{
    if (playback == NULL) return;
    (void)ma_device_stop(&playback->device);
    ma_device_uninit(&playback->device);
    ma_context_uninit(&playback->context);
    free(playback);
}
