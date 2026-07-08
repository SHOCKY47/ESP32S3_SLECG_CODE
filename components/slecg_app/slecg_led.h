/*
 * slecg_led.h
 * 双色 LED 指示：模式选择 + 采集状态。
 */
#ifndef SLECG_LED_H
#define SLECG_LED_H

#include "freertos/FreeRTOS.h"
#include "slecg_state.h"

/** @brief 初始化 GPIO41/42 并启动 LED 刷新任务 */
void slecg_led_start(const slecg_runtime_t *runtime);

/** @brief 触发错误提示：当前模式灯 200ms 快闪若干次 */
void slecg_led_show_error_hint(void);

#endif /* SLECG_LED_H */
