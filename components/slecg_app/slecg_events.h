/*
 * slecg_events.h
 * 应用层事件定义，由按键/BLE 产生，FSM 任务消费。
 */
#ifndef SLECG_EVENTS_H
#define SLECG_EVENTS_H

#include <stdint.h>

typedef enum {
    SLECG_EVT_BTN_SHORT = 0,
    SLECG_EVT_BTN_LONG,
    SLECG_EVT_BLE_START,
    SLECG_EVT_BLE_STOP,
    SLECG_EVT_BLE_REQ_STATUS,
} slecg_event_id_t;

typedef struct {
    slecg_event_id_t id;
    uint8_t orig_type;
    uint8_t start_mode;
    uint8_t start_flags;
} slecg_event_t;

#endif /* SLECG_EVENTS_H */
