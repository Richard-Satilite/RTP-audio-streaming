#include "ac/capture.h"
#include "ac/codec.h"
#include "ac/common.h"
#include "ac/net.h"
#include "ac/ring_buffer.h"
#include "ac/rtp.h"
#include "ac/thread.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AC_RTP_HEADER_LEN 12u
#define AC_DEFAULT_FRAME_MS_X10 50
#define AC_CAPTURE_RING_MS 500
#define AC_CAPTURE_PERIOD_MS 5

static uint32_t ac_sender_make_ssrc(void)
{
    uint64_t t = (uint64_t)time(NULL);
    uint32_t x = (uint32_t)(t ^ (t >> 32));
    x ^= 0x9e3779b9u;
    if (x == 0) x = 1;
    return x;
}

typedef struct {
    ac_udp_socket_t socket;
    ac_net_endpoint_t destination;
    ac_ring_buffer_t audio_ring;
    ac_thread_t network_thread;
    atomic_bool running;
    size_t frames_per_packet;
    size_t accum_frames;
    int16_t *accum;
    uint8_t payload[AC_MAX_PKT];
    uint8_t packet[AC_MAX_PKT];
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
    uint64_t packets_sent;
    uint64_t packets_failed;
    atomic_uint_fast64_t capture_dropped_frames;
    atomic_uint_fast64_t capture_frames;
} ac_sender_t;

static void ac_sender_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s <ip:port> [--frame-ms 5|2.5] [--device substring] [--list-devices]\n",
            program);
}

static int ac_sender_parse_destination(const char *text, ac_net_endpoint_t *out)
{
    const char *colon;
    char *end;
    unsigned long port;
    size_t ip_len;
    if (text == NULL || out == NULL) return -1;
    colon = strrchr(text, ':');
    if (colon == NULL || colon == text || colon[1] == '\0') return -1;
    ip_len = (size_t)(colon - text);
    if (ip_len >= sizeof(out->ip)) return -1;
    errno = 0;
    port = strtoul(colon + 1, &end, 10);
    if (errno != 0 || *end != '\0' || port == 0 || port > UINT16_MAX) return -1;
    memcpy(out->ip, text, ip_len);
    out->ip[ip_len] = '\0';
    out->port = (uint16_t)port;
    return 0;
}

static int ac_sender_parse_frame_ms_x10(const char *text, int *out)
{
    char *end;
    double value;
    int frame_ms_x10;
    if (text == NULL || out == NULL) return -1;
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || *end != '\0' || value <= 0.0) return -1;
    frame_ms_x10 = (int)(value * 10.0 + 0.5);
    if (frame_ms_x10 <= 0) return -1;
    *out = frame_ms_x10;
    return 0;
}

static int ac_sender_send_packet(ac_sender_t *sender)
{
    size_t payload_len;
    size_t packet_len;
    ac_rtp_packet_t rtp;

    payload_len = ac_l16_encode(sender->accum,
                                sender->frames_per_packet,
                                sender->payload,
                                sizeof(sender->payload));
    if (payload_len == 0) {
        sender->packets_failed++;
        return -1;
    }

    rtp.pt = AC_PT_L16;
    rtp.marker = sender->packets_sent == 0;
    rtp.seq = sender->seq++;
    rtp.ts = sender->ts;
    rtp.ssrc = sender->ssrc;
    rtp.payload = sender->payload;
    rtp.payload_len = payload_len;

    packet_len = ac_rtp_write(sender->packet, sizeof(sender->packet), &rtp);
    if (packet_len == 0) {
        sender->packets_failed++;
        return -1;
    }

    if (ac_net_udp_send_to(&sender->socket,
                           &sender->destination,
                           sender->packet,
                           packet_len) != AC_NET_OK) {
        sender->packets_failed++;
        return -1;
    }

    sender->ts += (uint32_t)sender->frames_per_packet;
    sender->packets_sent++;
    return 0;
}

static void *ac_sender_network_thread(void *arg)
{
    ac_sender_t *sender = (ac_sender_t *)arg;
    const size_t frame_bytes = AC_BYTES_PER_FRAME;

    while (atomic_load_explicit(&sender->running, memory_order_acquire)) {
        size_t available_bytes = ac_ring_buffer_available(&sender->audio_ring);
        size_t needed_frames = sender->frames_per_packet - sender->accum_frames;
        size_t available_frames = available_bytes / frame_bytes;

        if (available_frames == 0) {
            ac_thread_sleep_ms(1);
            continue;
        }

        if (available_frames > needed_frames) available_frames = needed_frames;

        {
            size_t samples = available_frames * AC_CHANNELS;
            size_t bytes = samples * sizeof(int16_t);
            size_t read = ac_ring_buffer_read(&sender->audio_ring,
                                               sender->accum + sender->accum_frames * AC_CHANNELS,
                                               bytes);
            size_t frames_read = read / frame_bytes;
            sender->accum_frames += frames_read;
        }

        if (sender->accum_frames == sender->frames_per_packet) {
            (void)ac_sender_send_packet(sender);
            sender->accum_frames = 0;
        }
    }

    /* Flush complete packets that were already captured before shutdown. */
    while (1) {
        size_t available_frames = ac_ring_buffer_available(&sender->audio_ring) / frame_bytes;
        size_t needed_frames = sender->frames_per_packet - sender->accum_frames;
        if (available_frames == 0) break;
        if (available_frames > needed_frames) available_frames = needed_frames;
        {
            size_t bytes = available_frames * frame_bytes;
            size_t read = ac_ring_buffer_read(&sender->audio_ring,
                                               sender->accum + sender->accum_frames * AC_CHANNELS,
                                               bytes);
            sender->accum_frames += read / frame_bytes;
        }
        if (sender->accum_frames == sender->frames_per_packet) {
            (void)ac_sender_send_packet(sender);
            sender->accum_frames = 0;
        }
    }

    return NULL;
}

static void ac_sender_capture_callback(const int16_t *pcm, size_t frames, void *user)
{
    ac_sender_t *sender = (ac_sender_t *)user;
    size_t free_frames;
    size_t frames_to_write;
    size_t bytes;

    if (sender == NULL || pcm == NULL || frames == 0) return;

    atomic_fetch_add_explicit(&sender->capture_frames, frames, memory_order_relaxed);
    free_frames = ac_ring_buffer_free(&sender->audio_ring) / AC_BYTES_PER_FRAME;
    frames_to_write = frames < free_frames ? frames : free_frames;
    bytes = frames_to_write * AC_BYTES_PER_FRAME;

    if (bytes > 0) {
        size_t written = ac_ring_buffer_write(&sender->audio_ring, pcm, bytes);
        size_t written_frames = written / AC_BYTES_PER_FRAME;
        if (written_frames < frames) {
            atomic_fetch_add_explicit(&sender->capture_dropped_frames,
                                      frames - written_frames,
                                      memory_order_relaxed);
        }
    } else {
        atomic_fetch_add_explicit(&sender->capture_dropped_frames,
                                  frames,
                                  memory_order_relaxed);
    }
}

int main(int argc, char **argv)
{
    ac_sender_t sender;
    ac_capture_t *capture = NULL;
    ac_capture_config_t capture_config;
    const char *device_substr = NULL;
    int frame_ms_x10 = AC_DEFAULT_FRAME_MS_X10;
    int list_devices = 0;
    size_t payload_len;
    int result = 1;

    memset(&sender, 0, sizeof(sender));
    sender.socket.fd = -1;
    atomic_init(&sender.running, false);
    atomic_init(&sender.capture_dropped_frames, 0);
    atomic_init(&sender.capture_frames, 0);

    if (argc < 2) {
        ac_sender_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list-devices") == 0) {
            list_devices = 1;
        }
    }

    /* A dummy endpoint is sufficient for device enumeration. */
    if (ac_sender_parse_destination(argv[1], &sender.destination) != 0) {
        if (!list_devices) {
            fprintf(stderr, "Invalid destination: %s\n", argv[1]);
            ac_sender_usage(argv[0]);
            return 1;
        }
        strcpy(sender.destination.ip, "127.0.0.1");
        sender.destination.port = 5004;
    }

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--frame-ms") == 0) {
            if (i + 1 >= argc ||
                ac_sender_parse_frame_ms_x10(argv[++i], &frame_ms_x10) != 0) {
                fprintf(stderr, "Invalid --frame-ms value.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--device") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing --device value.\n");
                return 1;
            }
            device_substr = argv[++i];
        } else if (strcmp(argv[i], "--list-devices") == 0) {
            /* handled above */
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            ac_sender_usage(argv[0]);
            return 1;
        }
    }

    if (list_devices) {
        if (ac_capture_list_devices() != 0) {
            fprintf(stderr, "Could not enumerate audio devices.\n");
            return 1;
        }
        return 0;
    }

    memset(&capture_config, 0, sizeof(capture_config));
    capture_config.device_substr = device_substr;
    capture_config.sample_rate = AC_SAMPLE_RATE;
    capture_config.channels = AC_CHANNELS;
    capture_config.period_ms = AC_CAPTURE_PERIOD_MS;
    capture_config.loopback = 1;
    capture_config.callback = ac_sender_capture_callback;
    capture_config.user = &sender;

    if (ac_capture_open(&capture, &capture_config) != 0) {
        fprintf(stderr, "Could not initialize system-audio loopback capture.\n");
        return 1;
    }

    sender.frames_per_packet = ac_codec_frames_for_duration(frame_ms_x10);
    if (sender.frames_per_packet == 0) {
        fprintf(stderr, "Invalid frame duration.\n");
        goto cleanup_capture;
    }

    payload_len = ac_l16_payload_size(sender.frames_per_packet);
    if (payload_len == 0 || payload_len + AC_RTP_HEADER_LEN > AC_MAX_PKT) {
        fprintf(stderr,
                "Frame duration is too large for AC_MAX_PKT: payload=%zu packet=%zu max=%d\n",
                payload_len,
                payload_len + AC_RTP_HEADER_LEN,
                AC_MAX_PKT);
        goto cleanup_capture;
    }

    sender.accum = (int16_t *)calloc(sender.frames_per_packet * AC_CHANNELS,
                                     sizeof(sender.accum[0]));
    if (sender.accum == NULL) {
        fprintf(stderr, "Could not allocate audio packet buffer.\n");
        goto cleanup_capture;
    }

    if (ac_ring_buffer_init(&sender.audio_ring,
                            (AC_SAMPLE_RATE * AC_BYTES_PER_FRAME * AC_CAPTURE_RING_MS) / 1000u + 1u) != 0) {
        fprintf(stderr, "Could not allocate audio ring buffer.\n");
        goto cleanup;
    }

    sender.ssrc = ac_sender_make_ssrc();

    if (ac_net_udp_open_sender(&sender.socket) != AC_NET_OK) {
        fprintf(stderr, "Could not open UDP sender socket.\n");
        goto cleanup;
    }

    atomic_store_explicit(&sender.running, true, memory_order_release);
    if (ac_thread_start(&sender.network_thread, ac_sender_network_thread, &sender) != 0) {
        atomic_store_explicit(&sender.running, false, memory_order_release);
        fprintf(stderr, "Could not start sender network thread.\n");
        goto cleanup;
    }

    if (ac_capture_start(capture) != 0) {
        fprintf(stderr, "Could not start system-audio capture.\n");
        atomic_store_explicit(&sender.running, false, memory_order_release);
        (void)ac_thread_join(&sender.network_thread);
        goto cleanup;
    }

    printf("Streaming system audio as L16/RTP/UDP to %s:%u (%zu frames/packet, %.1f ms).\n",
           sender.destination.ip,
           sender.destination.port,
           sender.frames_per_packet,
           frame_ms_x10 / 10.0);
    puts("Press Enter to stop.");

    (void)getchar();
    result = 0;

    atomic_store_explicit(&sender.running, false, memory_order_release);
    ac_capture_stop(capture);
    (void)ac_thread_join(&sender.network_thread);

cleanup:
    ac_net_udp_close(&sender.socket);
    ac_ring_buffer_uninit(&sender.audio_ring);
    free(sender.accum);

    printf("Packets sent: %llu, failed: %llu, captured frames: %llu, dropped frames: %llu\n",
           (unsigned long long)sender.packets_sent,
           (unsigned long long)sender.packets_failed,
           (unsigned long long)atomic_load_explicit(&sender.capture_frames, memory_order_relaxed),
           (unsigned long long)atomic_load_explicit(&sender.capture_dropped_frames, memory_order_relaxed));

cleanup_capture:
    if (capture != NULL) {
        ac_capture_stop(capture);
        ac_capture_close(capture);
    }
    return result;
}
