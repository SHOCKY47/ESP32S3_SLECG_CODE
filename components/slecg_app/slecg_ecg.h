/*
 * slecg_ecg.h
 * ECG 采集任务：DRDY 中断触发，500Hz 读帧并组包发送。
 */
#ifndef SLECG_ECG_H
#define SLECG_ECG_H

/** @brief 启动 DRDY ISR 与 ECG 采集任务 */
void slecg_ecg_start(void);

#endif /* SLECG_ECG_H */
