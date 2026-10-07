#include "ac/codec.h"
#include "ac/common.h"
#include "ac/jitter.h"
#include "ac/net.h"
#include "ac/playback.h"
#include "ac/ring_buffer.h"
#include "ac/rtp.h"
#include "ac/thread.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/time.h>
#endif

#define AC_RTP_HEADER_LEN 12u
#define AC_RECEIVER_RING_MS 500
#define AC_RECEIVER_SOCKET_TIMEOUT_MS 100
#define AC_DEFAULT_PREBUF_MS 60
#define AC_DEFAULT_FRAME_MS_X10 50
#define AC_DEFAULT_REORDER_PACKETS 3

typedef struct {
    ac_udp_socket_t socket;
    ac_ring_buffer_t playback_ring;
    ac_jitter_t jitter;
    ac_playback_t *playback;
    ac_thread_t network_thread;
    atomic_bool running;
    uint32_t lock_ssrc;
    uint64_t packets_invalid;
    uint64_t packets_wrong_pt;
    uint64_t packets_wrong_ssrc;
    uint64_t packets_udp_errors;
    uint64_t playback_dropped_frames;
    uint32_t frames_per_packet;
    size_t prebuffer_packets;
} ac_receiver_t;

static uint64_t ac_now_us(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    static int initialized;
    LARGE_INTEGER counter;
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000ULL) / frequency.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
#endif
}

static int ac_receiver_parse_port(const char *text, uint16_t *port)
{
    char *end;
    unsigned long value;
    if (text == NULL || port == NULL) return -1;
    value = strtoul(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value == 0 || value > UINT16_MAX) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int ac_receiver_parse_uint(const char *text, unsigned long *out)
{
    char *end;
    unsigned long value;
    if (text == NULL || out == NULL) return -1;
    value = strtoul(text, &end, 10);
    if (*text == '\0' || *end != '\0') return -1;
    *out = value;
    return 0;
}

static void ac_receiver_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s <port> [--prebuf-ms N] [--ssrc N] [--frame-ms 5|2.5]\n",
            program);
}

static int ac_receiver_queue_pcm(ac_receiver_t *receiver,
                                 const int16_t *pcm,
                                 size_t frames)
{
    size_t bytes = frames * AC_BYTES_PER_FRAME;
    size_t written = ac_ring_buffer_write(&receiver->playback_ring, pcm, bytes);
    size_t written_frames = written / AC_BYTES_PER_FRAME;
    if (written_frames < frames) {
        receiver->playback_dropped_frames += frames - written_frames;
        return -1;
    }
    return 0;
}

static int ac_receiver_queue_silence(ac_receiver_t *receiver, size_t frames)
{
    static int16_t silence[AC_SAMPLE_RATE / 100]; /* 10 ms */
    while (frames > 0) {
        size_t chunk = frames;
        if (chunk > (sizeof(silence) / sizeof(silence[0]) / AC_CHANNELS)) {
            chunk = sizeof(silence) / sizeof(silence[0]) / AC_CHANNELS;
        }
        if (ac_receiver_queue_pcm(receiver, silence, chunk) != 0) return -1;
        frames -= chunk;
    }
    return 0;
}

static void ac_receiver_drain_jitter(ac_receiver_t *receiver)
{
    uint8_t payload[AC_JITTER_PAYLOAD_CAP];
    int16_t pcm[AC_SAMPLE_RATE / 100 * AC_CHANNELS * 2];

    for (;;) {
        size_t payload_len = 0;
        uint16_t seq = 0;
        uint32_t ts = 0;
        uint32_t ssrc = 0;
        ac_jitter_pop_result_t result = ac_jitter_pop(&receiver->jitter,
                                                      payload,
                                                      sizeof(payload),
                                                      &payload_len,
                                                      &seq,
                                                      &ts,
                                                      &ssrc);
        (void)seq;
        (void)ts;
        (void)ssrc;
        if (result == AC_JITTER_POP_NONE) break;
        if (result == AC_JITTER_POP_LOST) {
            (void)ac_receiver_queue_silence(receiver, receiver->frames_per_packet);
            continue;
        }
        if (result == AC_JITTER_POP_PACKET) {
            size_t frames = ac_l16_decode(payload,
                                          payload_len,
                                          pcm,
                                          sizeof(pcm) / sizeof(pcm[0]));
            if (frames == 0) {
                receiver->packets_invalid++;
                continue;
            }
            (void)ac_receiver_queue_pcm(receiver, pcm, frames);
        }
    }
}

static void *ac_receiver_network_thread(void *arg)
{
    ac_receiver_t *receiver = (ac_receiver_t *)arg;
    uint8_t packet[AC_MAX_PKT];

    while (atomic_load_explicit(&receiver->running, memory_order_acquire)) {
        ac_net_endpoint_t from;
        int received = ac_net_udp_recv(&receiver->socket,
                                       packet,
                                       sizeof(packet),
                                       &from);
        if (received == AC_NET_TIMEOUT) continue;
        if (received < 0) {
            receiver->packets_udp_errors++;
            ac_thread_sleep_ms(1);
            continue;
        }

        {
            ac_rtp_packet_t rtp;
            if (ac_rtp_parse(packet, (size_t)received, &rtp) != 0) {
                receiver->packets_invalid++;
                continue;
            }
            if (rtp.pt != AC_PT_L16) {
                receiver->packets_wrong_pt++;
                continue;
            }
            if (receiver->lock_ssrc != 0 && rtp.ssrc != receiver->lock_ssrc) {
                receiver->packets_wrong_ssrc++;
                continue;
            }
            if (ac_jitter_push(&receiver->jitter, &rtp, ac_now_us()) != 0) {
                receiver->packets_invalid++;
                continue;
            }
            ac_receiver_drain_jitter(receiver);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    ac_receiver_t receiver;
    ac_playback_config_t playback_config;
    uint16_t port;
    int prebuf_ms = AC_DEFAULT_PREBUF_MS;
    int frame_ms_x10 = AC_DEFAULT_FRAME_MS_X10;
    size_t payload_len;
    int result = 1;

    memset(&receiver, 0, sizeof(receiver));
    receiver.socket.fd = -1;
    atomic_init(&receiver.running, false);

    if (argc < 2 || ac_receiver_parse_port(argv[1], &port) != 0) {
        ac_receiver_usage(argv[0]);
        return 1;
    }

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--prebuf-ms") == 0) {
            unsigned long value;
            if (i + 1 >= argc || ac_receiver_parse_uint(argv[++i], &value) != 0 || value == 0 || value > 5000) {
                fprintf(stderr, "Invalid --prebuf-ms value.\n");
                return 1;
            }
            prebuf_ms = (int)value;
        } else if (strcmp(argv[i], "--ssrc") == 0) {
            unsigned long value;
            if (i + 1 >= argc || ac_receiver_parse_uint(argv[++i], &value) != 0 || value > UINT32_MAX) {
                fprintf(stderr, "Invalid --ssrc value.\n");
                return 1;
            }
            receiver.lock_ssrc = (uint32_t)value;
        } else if (strcmp(argv[i], "--frame-ms") == 0) {
            char *end;
            double value;
            if (i + 1 >= argc) return 1;
            value = strtod(argv[++i], &end);
            if (*argv[i] == '\0' || *end != '\0' || value <= 0.0) return 1;
            frame_ms_x10 = (int)(value * 10.0 + 0.5);
        } else {
            ac_receiver_usage(argv[0]);
            return 1;
        }
    }

    receiver.frames_per_packet = (uint32_t)ac_codec_frames_for_duration(frame_ms_x10);
    if (receiver.frames_per_packet == 0) return 1;
    payload_len = ac_l16_payload_size(receiver.frames_per_packet);
    if (payload_len == 0 || payload_len + AC_RTP_HEADER_LEN > AC_MAX_PKT) {
        fprintf(stderr, "Invalid frame duration for RTP packet size.\n");
        return 1;
    }

    receiver.prebuffer_packets = ((size_t)prebuf_ms * 100u + (size_t)frame_ms_x10 - 1u) /
                                 (size_t)frame_ms_x10;
    if (receiver.prebuffer_packets < 2) receiver.prebuffer_packets = 2;
    if (receiver.prebuffer_packets > AC_JITTER_MAX_PACKETS / 2) {
        receiver.prebuffer_packets = AC_JITTER_MAX_PACKETS / 2;
    }

    if (ac_jitter_init(&receiver.jitter,
                       AC_JITTER_MAX_PACKETS,
                       receiver.prebuffer_packets,
                       AC_DEFAULT_REORDER_PACKETS,
                       AC_SAMPLE_RATE,
                       receiver.frames_per_packet) != 0) {
        fprintf(stderr, "Could not initialize jitter buffer.\n");
        return 1;
    }

    if (ac_ring_buffer_init(&receiver.playback_ring,
                            (AC_SAMPLE_RATE * AC_BYTES_PER_FRAME * AC_RECEIVER_RING_MS) / 1000u + 1u) != 0) {
        fprintf(stderr, "Could not allocate playback ring buffer.\n");
        return 1;
    }

    playback_config.sample_rate = AC_SAMPLE_RATE;
    playback_config.channels = AC_CHANNELS;
    playback_config.ring = &receiver.playback_ring;
    if (ac_playback_open(&receiver.playback, &playback_config) != 0) {
        fprintf(stderr, "Could not open local audio playback device.\n");
        goto cleanup;
    }

    if (ac_net_udp_open_receiver(&receiver.socket, port) != AC_NET_OK) {
        fprintf(stderr, "Could not bind UDP port %u.\n", port);
        goto cleanup;
    }
    (void)ac_net_udp_set_timeout(&receiver.socket, AC_RECEIVER_SOCKET_TIMEOUT_MS);

    atomic_store_explicit(&receiver.running, true, memory_order_release);
    if (ac_playback_start(receiver.playback) != 0) {
        fprintf(stderr, "Could not start local playback.\n");
        atomic_store_explicit(&receiver.running, false, memory_order_release);
        goto cleanup;
    }
    if (ac_thread_start(&receiver.network_thread, ac_receiver_network_thread, &receiver) != 0) {
        fprintf(stderr, "Could not start receiver thread.\n");
        atomic_store_explicit(&receiver.running, false, memory_order_release);
        goto cleanup;
    }

    printf("Listening for L16/RTP/UDP on 0.0.0.0:%u (%zu frames/packet, %.1f ms, prebuffer %d ms).\n",
           port,
           (size_t)receiver.frames_per_packet,
           frame_ms_x10 / 10.0,
           prebuf_ms);
    puts("Press Enter to stop.");
    (void)getchar();
    result = 0;

    atomic_store_explicit(&receiver.running, false, memory_order_release);
    (void)ac_thread_join(&receiver.network_thread);

cleanup:
    atomic_store_explicit(&receiver.running, false, memory_order_release);
    if (receiver.network_thread.started) (void)ac_thread_join(&receiver.network_thread);
    ac_playback_stop(receiver.playback);
    ac_net_udp_close(&receiver.socket);

    {
        ac_jitter_stats_t stats;
        ac_jitter_get_stats(&receiver.jitter, &stats);
        printf("RTP stats: received=%llu accepted=%llu lost=%llu late=%llu duplicate=%llu reordered=%llu malformed=%llu jitter=%.3f ms buffered=%zu\n",
               (unsigned long long)stats.packets_received,
               (unsigned long long)stats.packets_accepted,
               (unsigned long long)stats.packets_lost,
               (unsigned long long)stats.packets_late,
               (unsigned long long)stats.packets_duplicate,
               (unsigned long long)stats.packets_reordered,
               (unsigned long long)(stats.packets_malformed + receiver.packets_invalid),
               stats.jitter_ms,
               stats.buffered_packets);
        printf("Receiver errors: wrong_pt=%llu wrong_ssrc=%llu udp_errors=%llu playback_dropped_frames=%llu\n",
               (unsigned long long)receiver.packets_wrong_pt,
               (unsigned long long)receiver.packets_wrong_ssrc,
               (unsigned long long)receiver.packets_udp_errors,
               (unsigned long long)receiver.playback_dropped_frames);
    }

    ac_playback_close(receiver.playback);
    ac_ring_buffer_uninit(&receiver.playback_ring);
    return result;
}
