/*
 * slecg_app.c
 * 应用层启动编排：初始化各 FreeRTOS 任务与模块。
 */
#include "slecg_app.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "slecg_button.h"
#include "slecg_ecg.h"
#include "slecg_events.h"
#include "slecg_fsm.h"
#include "slecg_led.h"
#include "slecg_proto_handler.h"
#include "slecg_state.h"
#include "slecg_status.h"

static const char *TAG = "slecg_app";

#define SLECG_EVENT_QUEUE_LEN       16

static slecg_runtime_t s_runtime;
static QueueHandle_t s_event_queue;

void slecg_app_start(bool ads_ready)
{
    s_runtime.boot_time_us = esp_timer_get_time();

    s_event_queue = xQueueCreate(SLECG_EVENT_QUEUE_LEN, sizeof(slecg_event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "事件队列创建失败");
        return;
    }

    slecg_fsm_start(s_event_queue, &s_runtime, ads_ready);
    slecg_proto_handler_init(s_event_queue);
    slecg_button_start(s_event_queue);
    slecg_led_start(&s_runtime);
    slecg_ecg_start();
    slecg_status_start();

    ESP_LOGI(TAG, "SLECG 应用层已启动");
}
