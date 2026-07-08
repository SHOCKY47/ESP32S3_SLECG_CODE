/*
 * slecg_proto_handler.h
 * BLE 下行协议帧解析，将指令转为 FSM 事件。
 */
#ifndef SLECG_PROTO_HANDLER_H
#define SLECG_PROTO_HANDLER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/** @brief 注册 BLE RX 回调，解析后写入 event_queue */
void slecg_proto_handler_init(QueueHandle_t event_queue);

#endif /* SLECG_PROTO_HANDLER_H */
