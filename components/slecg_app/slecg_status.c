/*
 * slecg_status.c
 * DEVICE_STATUS 组装与发送。
 */
#include "slecg_status.h"

#include "ads129x.h"
#include "ble_slecg.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "slecg_fsm.h"
#include "slecg_proto_payload.h"
#include "slecg_proto_types.h"
#include "slecg_state.h"
#include "slecg_transport.h"

#define SLECG_STATUS_TASK_STACK     3072
#define SLECG_STATUS_TASK_PRIO      4
#define SLECG_STATUS_PERIOD_MS      1000

static uint8_t build_state_byte(const slecg_runtime_t *rt)
{
    uint8_t state = 0;

    if (rt->acq == SLECG_ACQ_RUNNING) {
        state |= SLECG_STATUS_BIT_ACQUIRING;
    }
    if (ble_slecg_is_connected()) {
        state |= SLECG_STATUS_BIT_BLE_CONNECTED;
    }
    if (rt->ads_ready) {
        state |= SLECG_STATUS_BIT_ADS_READY;
    }
    return state;
}

static void send_status_frame(void)
{
    const slecg_runtime_t *rt = slecg_fsm_get_runtime();
    uint8_t frame[SLECG_STATUS_FRAME_SIZE];
    slecg_status_payload_in_t in;
    size_t len;

    if (rt == NULL || !ble_slecg_is_connected() || !ble_slecg_is_notify_enabled()) {
        return;
    }

    in.state = build_state_byte(rt);
    in.error_code = rt->error_code;
    in.sample_rate_hz = (uint16_t)ADS129X_SAMPLE_RATE_HZ;
    in.ecg_seq = rt->ecg_seq;
    in.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    in.fw_version = SLECG_FW_VERSION;

    len = slecg_proto_build_status_frame(&in, frame, sizeof(frame));
    if (len > 0) {
        slecg_transport_send_ble_ctrl(frame, len);
    }
}

void slecg_status_send_once(void)
{
    send_status_frame();
}

static void status_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;

    while (true) {
        send_status_frame();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SLECG_STATUS_PERIOD_MS));
    }
}

void slecg_status_start(void)
{
    xTaskCreate(status_task, "slecg_status", SLECG_STATUS_TASK_STACK, NULL,
                SLECG_STATUS_TASK_PRIO, NULL);
}
