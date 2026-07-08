/*
 * slecg_proto_frame.h
 * SLECG 协议帧封装与流式解析。
 */
#ifndef SLECG_PROTO_FRAME_H
#define SLECG_PROTO_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "slecg_proto_types.h"

/** @brief 解析结果 */
typedef enum {
    SLECG_PARSE_NEED_MORE = 0,
    SLECG_PARSE_OK,
    SLECG_PARSE_INVALID,
} slecg_parse_result_t;

/** @brief 解析出的完整帧 */
typedef struct {
    uint8_t type;
    uint16_t payload_len;
    const uint8_t *payload;
} slecg_parsed_frame_t;

/**
 * @brief 组装完整协议帧到 out_buf。
 * @return 帧总字节数；0 表示缓冲区不足或参数无效。
 */
size_t slecg_frame_build(uint8_t type, const uint8_t *payload, uint16_t payload_len,
                          uint8_t *out_buf, size_t out_buf_size);

/** @brief 组装 ACK 帧 */
size_t slecg_frame_build_ack(uint8_t orig_type, uint8_t result,
                              uint8_t *out_buf, size_t out_buf_size);

/** @brief 组装 NACK 帧 */
size_t slecg_frame_build_nack(uint8_t orig_type, uint8_t error,
                               uint8_t *out_buf, size_t out_buf_size);

/**
 * @brief 流式喂入字节并尝试解析一帧。
 * @param cache 解析缓存（调用方持有，需清零初始化）
 * @param cache_len 当前缓存长度（入/出）
 * @param cache_cap cache 容量
 * @param byte 新字节
 * @param out 解析成功时输出帧信息
 */
slecg_parse_result_t slecg_frame_feed_byte(uint8_t *cache, size_t *cache_len, size_t cache_cap,
                                            uint8_t byte, slecg_parsed_frame_t *out);

#endif /* SLECG_PROTO_FRAME_H */
