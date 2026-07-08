/*
 * slecg_status.h
 * DEVICE_STATUS 周期上报（1Hz）与即时应答 REQ_STATUS。
 */
#ifndef SLECG_STATUS_H
#define SLECG_STATUS_H

/** @brief 启动 1Hz STATUS 任务 */
void slecg_status_start(void);

/** @brief 立即发送一包 DEVICE_STATUS（经 BLE） */
void slecg_status_send_once(void);

#endif /* SLECG_STATUS_H */
