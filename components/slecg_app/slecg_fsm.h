/*
 * slecg_fsm.h
 * 双维度状态机：传输模式 × 采集状态。
 */
#ifndef SLECG_FSM_H
#define SLECG_FSM_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "slecg_events.h"
#include "slecg_state.h"

/** @brief 启动 FSM 任务 */
void slecg_fsm_start(QueueHandle_t event_queue, slecg_runtime_t *runtime, bool ads_ready);

/** @brief 获取运行时状态（只读） */
const slecg_runtime_t *slecg_fsm_get_runtime(void);

/** @brief 更新全局 error_code */
void slecg_fsm_set_error(uint8_t error_code);

/** @brief 查看当前 ECG 包序号（不递增） */
uint16_t slecg_fsm_peek_ecg_seq(void);

/** @brief 递增 ECG 包序号并返回发送前的 seq */
uint16_t slecg_fsm_next_ecg_seq(void);

#endif /* SLECG_FSM_H */