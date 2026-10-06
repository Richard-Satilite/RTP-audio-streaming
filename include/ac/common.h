// common.h
#define AC_SAMPLE_RATE   48000
#define AC_CHANNELS      2
#define AC_BYTES_PER_FRAME (AC_CHANNELS * 2)   // native s16
#define AC_MAX_PKT       1400
#define AC_PT_L16        96                    // L16/48000/2
#define AC_PT_OPUS       97

typedef struct {
    // sender
    char     dest_ip[64];
    uint16_t dest_port;
    char     device_substr[128];
    int      frame_ms_x10;     // 25 = 2,5 ms; 50 = 5 ms (L16 stereo limit on the MTU)
    // receiver
    uint16_t listen_port;
    int      prebuf_ms;
    uint32_t lock_ssrc;        // 0 = locks onto the first valid SSRC
} ac_config_t;
