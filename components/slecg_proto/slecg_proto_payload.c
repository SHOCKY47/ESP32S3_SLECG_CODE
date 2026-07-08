/*
 * slecg_proto_payload.c
 * SLECG 各业务包 PAYLOAD 组装实现。
 */
#include "slecg_proto_payload.h"

#include <string.h>

#include "slecg_proto_frame.h"

static void write_le16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFFU);
    dst[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void write_le32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFFU);
    dst[1] = (uint8_t)((v >> 8) & 0xFFU);
    dst[2] = (uint8_t)((v >> 16) & 0xFFU);
    dst[3] = (uint8_t)((v >> 24) & 0xFFU);
}

size_t slecg_proto_build_ecg_frame(const slecg_ecg_payload_in_t *in,
                                    uint8_t *out_buf, size_t out_buf_size)
{
    uint8_t payload[SLECG_ECG_PAYLOAD_LEN];
    uint8_t i;

    if (in == NULL || in->samples == NULL) {
        return 0;
    }
    if (in->n_samples != SLECG_ECG_SAMPLES_PER_PKT) {
        return 0;
    }

    write_le16(&payload[0], in->seq);
    write_le32(&payload[2], in->ts_ms);
    payload[6] = in->n_samples;
    payload[7] = in->loff;

    for (i = 0; i < in->n_samples; ++i) {
        int16_t s = in->samples[i];
        payload[8 + i * 2] = (uint8_t)(s & 0xFF);
        payload[8 + i * 2 + 1] = (uint8_t)((uint16_t)s >> 8);
    }

    return slecg_frame_build(SLECG_TYPE_ECG_DATA, payload, SLECG_ECG_PAYLOAD_LEN,
                             out_buf, out_buf_size);
}

size_t slecg_proto_build_status_frame(const slecg_status_payload_in_t *in,
                                       uint8_t *out_buf, size_t out_buf_size)
{
    uint8_t payload[SLECG_STATUS_PAYLOAD_LEN];

    if (in == NULL) {
        return 0;
    }

    payload[0] = in->state;
    payload[1] = in->error_code;
    write_le16(&payload[2], in->sample_rate_hz);
    write_le16(&payload[4], in->ecg_seq);
    write_le32(&payload[6], in->uptime_ms);
    write_le16(&payload[10], in->fw_version);

    return slecg_frame_build(SLECG_TYPE_DEVICE_STATUS, payload, SLECG_STATUS_PAYLOAD_LEN,
                             out_buf, out_buf_size);
}
