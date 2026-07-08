/*
 * slecg_proto_handler.c
 * 流式解析 GATT Write 载荷并投递 FSM 事件。
 */
#include "slecg_proto_handler.h"

#include "ble_slecg.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "slecg_events.h"
#include "slecg_proto_frame.h"
#include "slecg_proto_types.h"

static const char *TAG = "slecg_proto";

static QueueHandle_t s_event_queue;
static uint8_t s_rx_cache[SLECG_FRAME_MAX_SIZE];
static size_t s_rx_cache_len;

static void post_event(const slecg_event_t *evt)
{
    if (s_event_queue != NULL) {
        xQueueSend(s_event_queue, evt, 0);
    }
}

static void handle_parsed_frame(const slecg_parsed_frame_t *frame)
{
    slecg_event_t evt = { 0 };

    switch (frame->type) {
    case SLECG_TYPE_START_ACQ:
        if (frame->payload_len != SLECG_START_ACQ_PAYLOAD_LEN) {
            return;
        }
        evt.id = SLECG_EVT_BLE_START;
        evt.orig_type = SLECG_TYPE_START_ACQ;
        evt.start_mode = frame->payload[0];
        evt.start_flags = frame->payload[1];
        post_event(&evt);
        break;
    case SLECG_TYPE_STOP_ACQ:
        if (frame->payload_len != SLECG_STOP_ACQ_PAYLOAD_LEN) {
            return;
        }
        evt.id = SLECG_EVT_BLE_STOP;
        evt.orig_type = SLECG_TYPE_STOP_ACQ;
        post_event(&evt);
        break;
    case SLECG_TYPE_REQ_STATUS:
        if (frame->payload_len != 0) {
            return;
        }
        evt.id = SLECG_EVT_BLE_REQ_STATUS;
        evt.orig_type = SLECG_TYPE_REQ_STATUS;
        post_event(&evt);
        break;
    default:
        ESP_LOGD(TAG, "忽略未知下行 TYPE=0x%02x", frame->type);
        break;
    }
}

static void ble_rx_cb(const uint8_t *data, uint16_t len, void *ctx)
{
    uint16_t i;
    slecg_parsed_frame_t parsed;

    (void)ctx;

    if (data == NULL || len == 0) {
        return;
    }

    for (i = 0; i < len; ++i) {
        slecg_parse_result_t r = slecg_frame_feed_byte(
            s_rx_cache, &s_rx_cache_len, sizeof(s_rx_cache), data[i], &parsed);
        if (r == SLECG_PARSE_OK) {
            handle_parsed_frame(&parsed);
        } else if (r == SLECG_PARSE_INVALID) {
            ESP_LOGW(TAG, "下行帧解析失败，重新同步");
        }
    }
}

void slecg_proto_handler_init(QueueHandle_t event_queue)
{
    s_event_queue = event_queue;
    s_rx_cache_len = 0;
    ble_slecg_set_rx_handler(ble_rx_cb, NULL);
    ESP_LOGI(TAG, "BLE 协议处理器已注册");
}
