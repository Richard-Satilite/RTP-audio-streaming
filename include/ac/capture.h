#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void (*ac_capture_callback_t)(const int16_t *pcm,
                                      size_t frames,
                                      void *user);

typedef struct ac_capture ac_capture_t;

typedef struct {
    const char *device_substr;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t period_ms;
    int loopback;
    ac_capture_callback_t callback;
    void *user;
} ac_capture_config_t;

int ac_capture_open(ac_capture_t **capture, const ac_capture_config_t *config);
int ac_capture_start(ac_capture_t *capture);
void ac_capture_stop(ac_capture_t *capture);
void ac_capture_close(ac_capture_t *capture);
int ac_capture_is_loopback_supported(ac_capture_t *capture);
int ac_capture_list_devices(void);
