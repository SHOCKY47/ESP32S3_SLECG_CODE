/*
 * slecg_proto_frame.c
 * SLECG 协议帧封装与流式解析实现。
 */
#include "slecg_proto_frame.h"

#include <string.h>

static size_t frame_total_len(uint16_t payload_len)
{
    return (size_t)payload_len + SLECG_FRAME_OVERHEAD;
}

static bool validate_footer(const uint8_t *frame, uint16_t payload_len)
{
    size_t foot = (size_t)payload_len + SLECG_FRAME_HEADER_SIZE;
    return frame[foot] == SLECG_FOOT_H && frame[foot + 1] == SLECG_FOOT_L;
}

size_t slecg_frame_build(uint8_t type, const uint8_t *payload, uint16_t payload_len,
                          uint8_t *out_buf, size_t out_buf_size)
{
    size_t total;

    if (out_buf == NULL || payload_len > SLECG_FRAME_MAX_PAYLOAD) {
        return 0;
    }
    if (payload_len > 0 && payload == NULL) {
        return 0;
    }

    total = frame_total_len(payload_len);
    if (out_buf_size < total) {
        return 0;
    }

    out_buf[0] = SLECG_SYNC_H;
    out_buf[1] = SLECG_SYNC_L;
    out_buf[2] = type;
    out_buf[3] = (uint8_t)(payload_len & 0xFFU);
    out_buf[4] = (uint8_t)((payload_len >> 8) & 0xFFU);
    if (payload_len > 0) {
        memcpy(&out_buf[5], payload, payload_len);
    }
    out_buf[5 + payload_len] = SLECG_FOOT_H;
    out_buf[6 + payload_len] = SLECG_FOOT_L;
    return total;
}

size_t slecg_frame_build_ack(uint8_t orig_type, uint8_t result,
                              uint8_t *out_buf, size_t out_buf_size)
{
    uint8_t payload[SLECG_ACK_PAYLOAD_LEN] = { orig_type, result };
    return slecg_frame_build(SLECG_TYPE_ACK, payload, SLECG_ACK_PAYLOAD_LEN,
                             out_buf, out_buf_size);
}

size_t slecg_frame_build_nack(uint8_t orig_type, uint8_t error,
                               uint8_t *out_buf, size_t out_buf_size)
{
    uint8_t payload[SLECG_NACK_PAYLOAD_LEN] = { orig_type, error };
    return slecg_frame_build(SLECG_TYPE_NACK, payload, SLECG_NACK_PAYLOAD_LEN,
                             out_buf, out_buf_size);
}

static slecg_parse_result_t try_parse_cache(uint8_t *cache, size_t *cache_len,
                                             slecg_parsed_frame_t *out)
{
    size_t len = *cache_len;
    size_t i;
    size_t total;
    uint16_t payload_len;

    /* 搜索 SYNC A5 5A */
    for (i = 0; i + 1 < len; ++i) {
        if (cache[i] == SLECG_SYNC_H && cache[i + 1] == SLECG_SYNC_L) {
            if (i > 0) {
                memmove(cache, &cache[i], len - i);
                len -= i;
                *cache_len = len;
            }
            break;
        }
    }
    if (len < 2 || cache[0] != SLECG_SYNC_H || cache[1] != SLECG_SYNC_L) {
        if (len > 1) {
            cache[0] = cache[len - 1];
            *cache_len = 1;
        }
        return SLECG_PARSE_NEED_MORE;
    }

    if (len < SLECG_FRAME_HEADER_SIZE) {
        return SLECG_PARSE_NEED_MORE;
    }

    payload_len = (uint16_t)cache[3] | ((uint16_t)cache[4] << 8);
    if (payload_len > SLECG_FRAME_MAX_PAYLOAD) {
        memmove(cache, &cache[1], len - 1);
        *cache_len = len - 1;
        return SLECG_PARSE_INVALID;
    }

    total = frame_total_len(payload_len);
    if (len < total) {
        return SLECG_PARSE_NEED_MORE;
    }

    if (!validate_footer(cache, payload_len)) {
        memmove(cache, &cache[1], len - 1);
        *cache_len = len - 1;
        return SLECG_PARSE_INVALID;
    }

    if (out != NULL) {
        out->type = cache[2];
        out->payload_len = payload_len;
        out->payload = (payload_len > 0) ? &cache[5] : NULL;
    }

    if (len > total) {
        memmove(cache, &cache[total], len - total);
    }
    *cache_len = len - total;
    return SLECG_PARSE_OK;
}

slecg_parse_result_t slecg_frame_feed_byte(uint8_t *cache, size_t *cache_len, size_t cache_cap,
                                            uint8_t byte, slecg_parsed_frame_t *out)
{
    if (cache == NULL || cache_len == NULL || *cache_len >= cache_cap) {
        return SLECG_PARSE_INVALID;
    }

    cache[*cache_len] = byte;
    (*cache_len)++;
    return try_parse_cache(cache, cache_len, out);
}
