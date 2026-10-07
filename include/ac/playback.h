#pragma once

#include <stdint.h>

#include "ac/ring_buffer.h"

typedef struct ac_playback ac_playback_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    ac_ring_buffer_t *ring;
} ac_playback_config_t;

int ac_playback_open(ac_playback_t **playback, const ac_playback_config_t *config);
int ac_playback_start(ac_playback_t *playback);
void ac_playback_stop(ac_playback_t *playback);
void ac_playback_close(ac_playback_t *playback);
