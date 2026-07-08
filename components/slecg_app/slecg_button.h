/*
 * slecg_button.h
 * GPIO18 按键：单击 / 长按 3s 检测。
 */
#ifndef SLECG_BUTTON_H
#define SLECG_BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "slecg_events.h"

/** @brief 启动按键扫描任务，事件写入 event_queue */
void slecg_button_start(QueueHandle_t event_queue);

#endif /* SLECG_BUTTON_H */
