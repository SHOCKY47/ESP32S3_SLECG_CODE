/*
 * slecg_ecg.c
 * ADS1291 DRDY 中断 + 500Hz 采样、25 点组包发送。
 */
#include "slecg_ecg.h"

#include "ads129x.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "slecg_fsm.h"
#include "slecg_proto_payload.h"
#include "slecg_proto_types.h"
#include "slecg_state.h"
#include "slecg_transport.h"

static const char *TAG = "slecg_ecg";

#define SLECG_ECG_TASK_STACK        4096
#define SLECG_ECG_TASK_PRIO         8
#define SLECG_DRDY_WAIT_MS          50

static SemaphoreHandle_t s_drdy_sem;

static void IRAM_ATTR drdy_isr_handler(void *arg)
{
    BaseType_t wake = pdFALSE;
    (void)arg;
    if (s_drdy_sem != NULL) {
        xSemaphoreGiveFromISR(s_drdy_sem, &wake);
        if (wake == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static void drdy_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_ADS_DRDY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);
    {
        esp_err_t isr_err = gpio_install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "GPIO ISR 服务安装失败: %s", esp_err_to_name(isr_err));
            return;
        }
    }
    gpio_isr_handler_add(BOARD_ADS_DRDY_GPIO, drdy_isr_handler, NULL);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static void ecg_task(void *arg)
{
    int16_t sample_buf[SLECG_ECG_SAMPLES_PER_PKT];
    uint8_t sample_count = 0;
    uint8_t last_loff = 0;
    uint8_t frame[SLECG_ECG_FRAME_SIZE];

    (void)arg;

    while (true) {
        const slecg_runtime_t *rt = slecg_fsm_get_runtime();

        if (rt == NULL || rt->acq != SLECG_ACQ_RUNNING) {
            sample_count = 0;
            xSemaphoreTake(s_drdy_sem, pdMS_TO_TICKS(SLECG_DRDY_WAIT_MS));
            continue;
        }

        if (xSemaphoreTake(s_drdy_sem, pdMS_TO_TICKS(SLECG_DRDY_WAIT_MS)) != pdTRUE) {
            continue;
        }

        ads129x_sample_t sample;
        if (ads129x_read_frame(&sample) != 0) {
            slecg_fsm_set_error(SLECG_ERR_SPI);
            continue;
        }

        sample_buf[sample_count] = sample.ch1_value;
        last_loff = sample.loff_status;
        sample_count++;

        if (sample_count < SLECG_ECG_SAMPLES_PER_PKT) {
            continue;
        }

        slecg_ecg_payload_in_t pkt = {
            .seq = slecg_fsm_next_ecg_seq(),
            .ts_ms = now_ms(),
            .loff = last_loff,
            .samples = sample_buf,
            .n_samples = SLECG_ECG_SAMPLES_PER_PKT,
        };

        size_t frame_len = slecg_proto_build_ecg_frame(&pkt, frame, sizeof(frame));
        sample_count = 0;

        if (frame_len == 0) {
            continue;
        }

        if (slecg_transport_send_data(rt, frame, frame_len) != ESP_OK) {
            slecg_fsm_set_error(SLECG_ERR_TX_QUEUE_FULL);
            ESP_LOGW(TAG, "ECG 帧发送失败");
        }
    }
}

void slecg_ecg_start(void)
{
    s_drdy_sem = xSemaphoreCreateCounting(32, 0);
    if (s_drdy_sem == NULL) {
        ESP_LOGE(TAG, "DRDY 信号量创建失败");
        return;
    }

    drdy_gpio_init();
    xTaskCreate(ecg_task, "slecg_ecg", SLECG_ECG_TASK_STACK, NULL, SLECG_ECG_TASK_PRIO, NULL);
}
