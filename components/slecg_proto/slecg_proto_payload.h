/*
 * slecg_proto_payload.h
 * SLECG 各业务包 PAYLOAD 组装。
 */
#ifndef SLECG_PROTO_PAYLOAD_H
#define SLECG_PROTO_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "slecg_proto_types.h"

/** @brief ECG_DATA 组包输入 */
typedef struct {
    uint16_t seq;
    uint32_t ts_ms;
    uint8_t loff;
    const int16_t *samples;
    uint8_t n_samples;
} slecg_ecg_payload_in_t;

/** @brief DEVICE_STATUS 组包输入 */
typedef struct {
    uint8_t state;
    uint8_t error_code;
    uint16_t sample_rate_hz;
    uint16_t ecg_seq;
    uint32_t uptime_ms;
    uint16_t fw_version;
} slecg_status_payload_in_t;

/** @brief 组装 ECG_DATA 完整帧 */
size_t slecg_proto_build_ecg_frame(const slecg_ecg_payload_in_t *in,
                                    uint8_t *out_buf, size_t out_buf_size);

/** @brief 组装 DEVICE_STATUS 完整帧 */
size_t slecg_proto_build_status_frame(const slecg_status_payload_in_t *in,
                                       uint8_t *out_buf, size_t out_buf_size);

#endif /* SLECG_PROTO_PAYLOAD_H */
