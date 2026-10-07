#include "ac/rtp.h"

#include <string.h>

#define AC_RTP_HEADER_LEN 12u
#define AC_RTP_VERSION 2u
#define AC_RTP_PT_MASK 0x7fu
#define AC_RTP_MARKER_MASK 0x80u

static void ac_rtp_write_u16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)((value >> 8) & 0xffu);
    buf[1] = (uint8_t)(value & 0xffu);
}

static void ac_rtp_write_u32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)((value >> 24) & 0xffu);
    buf[1] = (uint8_t)((value >> 16) & 0xffu);
    buf[2] = (uint8_t)((value >> 8) & 0xffu);
    buf[3] = (uint8_t)(value & 0xffu);
}

static uint16_t ac_rtp_read_u16(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

static uint32_t ac_rtp_read_u32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

size_t ac_rtp_write(uint8_t *buf, size_t cap, const ac_rtp_packet_t *packet)
{
    size_t needed;

    if (buf == NULL || packet == NULL) {
        return 0;
    }

    if (packet->pt > AC_RTP_PT_MASK) {
        return 0;
    }

    if (packet->payload_len > 0 && packet->payload == NULL) {
        return 0;
    }

    needed = AC_RTP_HEADER_LEN + packet->payload_len;
    if (cap < needed) {
        return 0;
    }

    buf[0] = (uint8_t)(AC_RTP_VERSION << 6);
    buf[1] = packet->pt & AC_RTP_PT_MASK;
    if (packet->marker) {
        buf[1] |= AC_RTP_MARKER_MASK;
    }

    ac_rtp_write_u16(&buf[2], packet->seq);
    ac_rtp_write_u32(&buf[4], packet->ts);
    ac_rtp_write_u32(&buf[8], packet->ssrc);

    if (packet->payload_len > 0) {
        memcpy(&buf[AC_RTP_HEADER_LEN], packet->payload, packet->payload_len);
    }

    return needed;
}

int ac_rtp_parse(const uint8_t *buf, size_t len, ac_rtp_packet_t *out)
{
    uint8_t version;
    uint8_t has_padding;
    uint8_t has_extension;
    uint8_t csrc_count;

    if (buf == NULL || out == NULL || len < AC_RTP_HEADER_LEN) {
        return -1;
    }

    version = (uint8_t)(buf[0] >> 6);
    has_padding = (uint8_t)((buf[0] >> 5) & 0x01u);
    has_extension = (uint8_t)((buf[0] >> 4) & 0x01u);
    csrc_count = (uint8_t)(buf[0] & 0x0fu);

    if (version != AC_RTP_VERSION) {
        return -1;
    }

    if (has_padding || has_extension || csrc_count != 0) {
        return -1;
    }

    out->marker = (buf[1] & AC_RTP_MARKER_MASK) != 0;
    out->pt = buf[1] & AC_RTP_PT_MASK;
    out->seq = ac_rtp_read_u16(&buf[2]);
    out->ts = ac_rtp_read_u32(&buf[4]);
    out->ssrc = ac_rtp_read_u32(&buf[8]);
    out->payload = &buf[AC_RTP_HEADER_LEN];
    out->payload_len = len - AC_RTP_HEADER_LEN;

    return 0;
}