/*
 * slecg_state.h
 * 运行时全局状态快照，供 FSM / LED / STATUS / ECG 共享。
 */
#ifndef SLECG_STATE_H
#define SLECG_STATE_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 数据传输通道 */
typedef enum {
    SLECG_TRANSPORT_UART = 0,
    SLECG_TRANSPORT_BLE,
} slecg_transport_mode_t;

/** @brief ECG 采集状态 */
typedef enum {
    SLECG_ACQ_IDLE = 0,
    SLECG_ACQ_RUNNING,
} slecg_acq_state_t;

typedef struct {
    slecg_transport_mode_t transport;
    slecg_acq_state_t acq;
    bool ads_ready;
    bool ads_ever_started;
    uint8_t error_code;
    uint16_t ecg_seq;
    int64_t boot_time_us;
} slecg_runtime_t;

#endif /* SLECG_STATE_H */
