/*
 * slecg_proto_types.h
 * SLECG 蓝牙/有线共用应用层协议常量（v1.0）。
 * 与 ble_protocol/ 文档保持一致。
 */
#ifndef SLECG_PROTO_TYPES_H
#define SLECG_PROTO_TYPES_H

#include <stdint.h>

/* 帧信封 */
#define SLECG_SYNC_H                0xA5U
#define SLECG_SYNC_L                0x5AU
#define SLECG_FOOT_H                0x5AU
#define SLECG_FOOT_L                0xA5U
#define SLECG_FRAME_HEADER_SIZE     5U   /* SYNC(2) + TYPE(1) + LEN(2) */
#define SLECG_FRAME_TRAILER_SIZE    2U   /* FOOT(2) */
#define SLECG_FRAME_OVERHEAD        (SLECG_FRAME_HEADER_SIZE + SLECG_FRAME_TRAILER_SIZE)
#define SLECG_FRAME_MAX_PAYLOAD     504U
#define SLECG_FRAME_MAX_SIZE        512U

/* 包类型 TYPE */
#define SLECG_TYPE_ACK              0x01U
#define SLECG_TYPE_NACK             0x02U
#define SLECG_TYPE_START_ACQ        0x10U
#define SLECG_TYPE_STOP_ACQ         0x11U
#define SLECG_TYPE_REQ_STATUS       0x12U
#define SLECG_TYPE_ECG_DATA         0x20U
#define SLECG_TYPE_DEVICE_STATUS    0x30U
#define SLECG_TYPE_IMU_DATA         0x40U

/* PAYLOAD 固定长度 */
#define SLECG_ACK_PAYLOAD_LEN       2U
#define SLECG_NACK_PAYLOAD_LEN      2U
#define SLECG_START_ACQ_PAYLOAD_LEN 2U
#define SLECG_STOP_ACQ_PAYLOAD_LEN  1U
#define SLECG_ECG_PAYLOAD_LEN       58U
#define SLECG_ECG_FRAME_SIZE        (SLECG_FRAME_OVERHEAD + SLECG_ECG_PAYLOAD_LEN)
#define SLECG_STATUS_PAYLOAD_LEN    12U
#define SLECG_STATUS_FRAME_SIZE     (SLECG_FRAME_OVERHEAD + SLECG_STATUS_PAYLOAD_LEN)

/* ECG 组包 */
#define SLECG_ECG_SAMPLES_PER_PKT   25U

/* NACK 错误码 */
#define SLECG_ERR_NONE              0U
#define SLECG_ERR_SPI               1U
#define SLECG_ERR_DRDY_TIMEOUT      2U
#define SLECG_ERR_TX_QUEUE_FULL     3U
#define SLECG_ERR_STATE_CONFLICT    4U
#define SLECG_ERR_INVALID_PARAM     5U

/* START_ACQ mode */
#define SLECG_START_MODE_NORMAL     0x00U

/* DEVICE_STATUS state 位掩码 */
#define SLECG_STATUS_BIT_ACQUIRING      (1U << 0)
#define SLECG_STATUS_BIT_BLE_CONNECTED  (1U << 1)
#define SLECG_STATUS_BIT_ADS_READY      (1U << 2)
#define SLECG_STATUS_BIT_IMU_READY      (1U << 3)

/* 固件版本：major<<8 | minor */
#define SLECG_FW_VERSION            0x0100U

#endif /* SLECG_PROTO_TYPES_H */
