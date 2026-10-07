#include "ac/capture.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniaudio.h"

struct ac_capture {
    ma_context context;
    ma_device device;
    ma_device_id device_id;
    int has_device_id;
    int loopback;
    ac_capture_callback_t callback;
    void *user;
};

static int ac_contains_ci(const char *text, const char *needle)
{
    size_t needle_len;
    if (text == NULL || needle == NULL || needle[0] == '\0') return 0;
    needle_len = strlen(needle);
    for (; *text != '\0'; ++text) {
        size_t i;
        for (i = 0; i < needle_len && text[i] != '\0'; ++i) {
            if (tolower((unsigned char)text[i]) !=
                tolower((unsigned char)needle[i])) break;
        }
        if (i == needle_len) return 1;
    }
    return 0;
}

static void ac_capture_data_callback(ma_device *device,
                                     void *output,
                                     const void *input,
                                     ma_uint32 frame_count)
{
    ac_capture_t *capture = (ac_capture_t *)device->pUserData;
    (void)output;
    if (capture == NULL || input == NULL || capture->callback == NULL) return;
    capture->callback((const int16_t *)input, (size_t)frame_count, capture->user);
}

static int ac_capture_find_device(ac_capture_t *capture, const char *device_substr)
{
    ma_device_info *playback_infos = NULL;
    ma_device_info *capture_infos = NULL;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    ma_uint32 i;

    if (ma_context_get_devices(&capture->context,
                               &playback_infos,
                               &playback_count,
                               &capture_infos,
                               &capture_count) != MA_SUCCESS) {
        return -1;
    }

#ifdef _WIN32
    /* WASAPI loopback uses the playback endpoint as its source. */
    if (capture->loopback) {
        if (device_substr != NULL && device_substr[0] != '\0') {
            for (i = 0; i < playback_count; ++i) {
                if (strstr(playback_infos[i].name, device_substr) != NULL) {
                    capture->device_id = playback_infos[i].id;
                    capture->has_device_id = 1;
                    return 0;
                }
            }
            return -1;
        }
        for (i = 0; i < playback_count; ++i) {
            if (playback_infos[i].isDefault) {
                capture->device_id = playback_infos[i].id;
                capture->has_device_id = 1;
                return 0;
            }
        }
        return playback_count > 0 ? 0 : -1;
    }
#else
    /* PulseAudio/PipeWire compatibility exposes output monitors as capture sources. */
    if (capture->loopback) {
        if (device_substr != NULL && device_substr[0] != '\0') {
            for (i = 0; i < capture_count; ++i) {
                if (strstr(capture_infos[i].name, device_substr) != NULL) {
                    capture->device_id = capture_infos[i].id;
                    capture->has_device_id = 1;
                    return 0;
                }
            }
            return -1;
        }
        for (i = 0; i < capture_count; ++i) {
            if (ac_contains_ci(capture_infos[i].name, "monitor")) {
                capture->device_id = capture_infos[i].id;
                capture->has_device_id = 1;
                return 0;
            }
        }
        return -1;
    }
#endif

    if (device_substr == NULL || device_substr[0] == '\0') return 0;
    for (i = 0; i < capture_count; ++i) {
        if (strstr(capture_infos[i].name, device_substr) != NULL) {
            capture->device_id = capture_infos[i].id;
            capture->has_device_id = 1;
            return 0;
        }
    }
    return -1;
}

int ac_capture_open(ac_capture_t **capture, const ac_capture_config_t *config)
{
    ac_capture_t *new_capture;
    ma_device_config device_config;

    if (capture == NULL || config == NULL || config->callback == NULL ||
        config->sample_rate == 0 || config->channels == 0) return -1;

    *capture = NULL;
    new_capture = (ac_capture_t *)calloc(1, sizeof(*new_capture));
    if (new_capture == NULL) return -1;

    new_capture->callback = config->callback;
    new_capture->user = config->user;
    new_capture->loopback = config->loopback != 0;

    if (ma_context_init(NULL, 0, NULL, &new_capture->context) != MA_SUCCESS) {
        free(new_capture);
        return -1;
    }

    if (new_capture->loopback && !ma_context_is_loopback_supported(&new_capture->context)) {
#ifdef _WIN32
        fprintf(stderr, "Audio loopback is not supported by the selected miniaudio backend.\n");
#else
        fprintf(stderr, "Native loopback is unavailable; looking for a PulseAudio/PipeWire monitor source.\n");
#endif
#ifndef _WIN32
        /* Continue: PulseAudio monitor sources are exposed as normal capture devices. */
#endif
    }

    if (ac_capture_find_device(new_capture, config->device_substr) != 0) {
#ifndef _WIN32
        if (new_capture->loopback) {
            fprintf(stderr,
                    "No output-monitor source found. On Linux, use PipeWire/PulseAudio and pass --device with the monitor name.\n");
        }
#endif
        ma_context_uninit(&new_capture->context);
        free(new_capture);
        return -1;
    }

#ifdef _WIN32
    device_config = ma_device_config_init(new_capture->loopback ?
                                           ma_device_type_loopback :
                                           ma_device_type_capture);
#else
    /* On Unix the selected PulseAudio/PipeWire monitor is opened as capture. */
    device_config = ma_device_config_init(ma_device_type_capture);
#endif

    device_config.capture.format = ma_format_s16;
    device_config.capture.channels = config->channels;
    device_config.sampleRate = config->sample_rate;
    device_config.dataCallback = ac_capture_data_callback;
    device_config.pUserData = new_capture;
    if (config->period_ms > 0) device_config.periodSizeInMilliseconds = config->period_ms;

    if (new_capture->has_device_id) {
        device_config.capture.pDeviceID = &new_capture->device_id;
    }

#ifdef _WIN32
    if (new_capture->loopback) {
        device_config.wasapi.loopbackProcessID = 0;
        device_config.wasapi.loopbackProcessExclude = MA_FALSE;
    }
#endif

    if (ma_device_init(&new_capture->context,
                       &device_config,
                       &new_capture->device) != MA_SUCCESS) {
        ma_context_uninit(&new_capture->context);
        free(new_capture);
        return -1;
    }

    *capture = new_capture;
    return 0;
}

int ac_capture_start(ac_capture_t *capture)
{
    if (capture == NULL) return -1;
    return ma_device_start(&capture->device) == MA_SUCCESS ? 0 : -1;
}

void ac_capture_stop(ac_capture_t *capture)
{
    if (capture == NULL) return;
    (void)ma_device_stop(&capture->device);
}

void ac_capture_close(ac_capture_t *capture)
{
    if (capture == NULL) return;
    (void)ma_device_stop(&capture->device);
    ma_device_uninit(&capture->device);
    ma_context_uninit(&capture->context);
    free(capture);
}

int ac_capture_is_loopback_supported(ac_capture_t *capture)
{
    if (capture == NULL) return 0;
    return ma_context_is_loopback_supported(&capture->context) ? 1 : 0;
}

int ac_capture_list_devices(void)
{
    ma_context context;
    ma_device_info *playback_infos = NULL;
    ma_device_info *capture_infos = NULL;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    ma_uint32 i;

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) return -1;
    if (ma_context_get_devices(&context,
                               &playback_infos,
                               &playback_count,
                               &capture_infos,
                               &capture_count) != MA_SUCCESS) {
        ma_context_uninit(&context);
        return -1;
    }

    puts("Playback devices:");
    for (i = 0; i < playback_count; ++i) {
        printf("  %s%s\n", playback_infos[i].name,
               playback_infos[i].isDefault ? " [default]" : "");
    }
    puts("Capture/monitor devices:");
    for (i = 0; i < capture_count; ++i) {
        printf("  %s%s\n", capture_infos[i].name,
               capture_infos[i].isDefault ? " [default]" : "");
    }

    ma_context_uninit(&context);
    return 0;
}
