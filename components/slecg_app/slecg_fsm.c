/*
 * slecg_fsm.c
 * 状态机：处理按键/BLE 事件，控制 ADS1291 与 UART 日志策略。
 */
#include "slecg_fsm.h"

#include "ads129x.h"
#include "ble_slecg.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "slecg_led.h"
#include "slecg_nvs.h"
#include "slecg_proto_types.h"
#include "slecg_proto_frame.h"
#include "slecg_status.h"
#include "slecg_transport.h"
#include "slecg_uart_stream.h"

static const char *TAG = "slecg_fsm";

#define SLECG_FSM_TASK_STACK        4096
#define SLECG_FSM_TASK_PRIO         6
#define SLECG_FSM_QUEUE_LEN         16

static QueueHandle_t s_event_queue;
static slecg_runtime_t *s_runtime;

static void send_nack(uint8_t orig_type, uint8_t error)
{
    uint8_t frame[SLECG_FRAME_OVERHEAD + SLECG_NACK_PAYLOAD_LEN];
    size_t len = slecg_frame_build_nack(orig_type, error, frame, sizeof(frame));
    if (len > 0) {
        slecg_transport_send_ble_ctrl(frame, len);
    }
}

static void send_ack(uint8_t orig_type)
{
    uint8_t frame[SLECG_FRAME_OVERHEAD + SLECG_ACK_PAYLOAD_LEN];
    size_t len = slecg_frame_build_ack(orig_type, 0, frame, sizeof(frame));
    if (len > 0) {
        slecg_transport_send_ble_ctrl(frame, len);
    }
}

static void apply_uart_log_policy(void)
{
    if (s_runtime->transport == SLECG_TRANSPORT_UART &&
        s_runtime->acq == SLECG_ACQ_RUNNING) {
        slecg_uart_stream_logs_disable();
    } else {
        slecg_uart_stream_logs_enable();
    }
}

static int ads_start_hw(void)
{
    int ret;

    if (!s_runtime->ads_ever_started) {
        ret = ads129x_init_start();
    } else {
        ret = ads129x_start();
    }
    if (ret == 0) {
        s_runtime->ads_ever_started = true;
    }
    return ret;
}

static void ads_stop_hw(void)
{
    ads129x_stop();
}

static bool ble_ready_for_acq(void)
{
    return ble_slecg_is_connected() && ble_slecg_is_notify_enabled();
}

static void do_start_acq(uint8_t orig_type, bool from_ble)
{
    int ret;

    if (from_ble && s_runtime->transport != SLECG_TRANSPORT_BLE) {
        send_nack(orig_type, SLECG_ERR_STATE_CONFLICT);
        return;
    }

    if (s_runtime->acq == SLECG_ACQ_RUNNING) {
        if (from_ble) {
            send_nack(orig_type, SLECG_ERR_STATE_CONFLICT);
        }
        return;
    }

    if (!s_runtime->ads_ready) {
        if (from_ble) {
            send_nack(orig_type, SLECG_ERR_SPI);
        } else {
            slecg_led_show_error_hint();
        }
        return;
    }

    if (s_runtime->transport == SLECG_TRANSPORT_BLE && !ble_ready_for_acq()) {
        if (from_ble) {
            send_nack(orig_type, SLECG_ERR_STATE_CONFLICT);
        } else {
            slecg_led_show_error_hint();
        }
        return;
    }

    if (s_runtime->transport == SLECG_TRANSPORT_UART) {
        slecg_uart_stream_flush();
    }

    ret = ads_start_hw();
    if (ret != 0) {
        s_runtime->error_code = SLECG_ERR_SPI;
        if (from_ble) {
            send_nack(orig_type, SLECG_ERR_SPI);
        } else {
            slecg_led_show_error_hint();
        }
        return;
    }

    s_runtime->acq = SLECG_ACQ_RUNNING;
    s_runtime->error_code = SLECG_ERR_NONE;
    apply_uart_log_policy();

    if (from_ble) {
        send_ack(orig_type);
    }
    ESP_LOGI(TAG, "采集已开始，传输=%s",
             s_runtime->transport == SLECG_TRANSPORT_UART ? "UART" : "BLE");
}

static void do_stop_acq(uint8_t orig_type, bool from_ble)
{
    if (from_ble && s_runtime->transport != SLECG_TRANSPORT_BLE) {
        send_nack(orig_type, SLECG_ERR_STATE_CONFLICT);
        return;
    }

    if (s_runtime->acq == SLECG_ACQ_IDLE) {
        if (from_ble) {
            send_ack(orig_type);
        }
        return;
    }

    ads_stop_hw();
    s_runtime->acq = SLECG_ACQ_IDLE;
    apply_uart_log_policy();

    if (from_ble) {
        send_ack(orig_type);
    }
    ESP_LOGI(TAG, "采集已停止");
}

static void toggle_acq_by_button(void)
{
    if (s_runtime->acq == SLECG_ACQ_RUNNING) {
        do_stop_acq(0, false);
    } else {
        do_start_acq(0, false);
    }
}

static void switch_transport_mode(void)
{
    slecg_transport_mode_t next;

    if (s_runtime->acq == SLECG_ACQ_RUNNING) {
        do_stop_acq(0, false);
    }

    next = (s_runtime->transport == SLECG_TRANSPORT_UART) ?
           SLECG_TRANSPORT_BLE : SLECG_TRANSPORT_UART;
    s_runtime->transport = next;
    apply_uart_log_policy();

    if (slecg_nvs_save_transport(next) != ESP_OK) {
        ESP_LOGW(TAG, "传输模式 NVS 保存失败");
    }

    ESP_LOGI(TAG, "传输模式切换为 %s",
             next == SLECG_TRANSPORT_UART ? "UART" : "BLE");
}

static void handle_event(const slecg_event_t *evt)
{
    switch (evt->id) {
    case SLECG_EVT_BTN_SHORT:
        toggle_acq_by_button();
        break;
    case SLECG_EVT_BTN_LONG:
        switch_transport_mode();
        break;
    case SLECG_EVT_BLE_START:
        if (evt->start_mode != SLECG_START_MODE_NORMAL) {
            send_nack(SLECG_TYPE_START_ACQ, SLECG_ERR_INVALID_PARAM);
            break;
        }
        do_start_acq(SLECG_TYPE_START_ACQ, true);
        break;
    case SLECG_EVT_BLE_STOP:
        do_stop_acq(SLECG_TYPE_STOP_ACQ, true);
        break;
    case SLECG_EVT_BLE_REQ_STATUS:
        slecg_status_send_once();
        break;
    default:
        break;
    }
}

static void fsm_task(void *arg)
{
    slecg_event_t evt;

    (void)arg;

    while (true) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            handle_event(&evt);
        }
    }
}

void slecg_fsm_start(QueueHandle_t event_queue, slecg_runtime_t *runtime, bool ads_ready)
{
    slecg_transport_mode_t mode = SLECG_TRANSPORT_UART;

    s_event_queue = event_queue;
    s_runtime = runtime;

    runtime->transport = SLECG_TRANSPORT_UART;
    runtime->acq = SLECG_ACQ_IDLE;
    runtime->ads_ready = ads_ready;
    runtime->ads_ever_started = false;
    runtime->error_code = SLECG_ERR_NONE;
    runtime->ecg_seq = 0;

    if (slecg_nvs_load_transport(&mode) == ESP_OK) {
        runtime->transport = mode;
    }

    apply_uart_log_policy();

    xTaskCreate(fsm_task, "slecg_fsm", SLECG_FSM_TASK_STACK, NULL, SLECG_FSM_TASK_PRIO, NULL);
}

const slecg_runtime_t *slecg_fsm_get_runtime(void)
{
    return s_runtime;
}

void slecg_fsm_set_error(uint8_t error_code)
{
    if (s_runtime != NULL) {
        s_runtime->error_code = error_code;
    }
}

uint16_t slecg_fsm_next_ecg_seq(void)
{
    uint16_t seq = 0;
    if (s_runtime != NULL) {
        seq = s_runtime->ecg_seq;
        s_runtime->ecg_seq++;
    }
    return seq;
}