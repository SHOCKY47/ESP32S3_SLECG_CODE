/*
 * slecg_app.h
 * SLECG 应用层统一入口。
 */
#ifndef SLECG_APP_H
#define SLECG_APP_H

#include <stdbool.h>

/** @brief 创建任务/队列并启动完整应用（按键、FSM、ECG、STATUS 等） */
void slecg_app_start(bool ads_ready);

#endif /* SLECG_APP_H */
