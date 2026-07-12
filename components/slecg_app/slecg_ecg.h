/*
 * slecg_ecg.h
 * ECG 采集任务：DRDY 中断触发，按 ADS129X_SAMPLE_RATE_HZ 读帧并组包发送。
 */
#ifndef SLECG_ECG_H
#define SLECG_ECG_H

/** @brief 启动 DRDY ISR 与 ECG 采集任务 */
void slecg_ecg_start(void);

/** @brief 清零采集诊断计数（开始采集前调用） */
void slecg_ecg_reset_stats(void);

/** @brief 打印采集诊断计数（停止采集、日志恢复后调用） */
void slecg_ecg_log_stats(void);

#endif /* SLECG_ECG_H */
