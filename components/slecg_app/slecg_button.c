/*
 * slecg_button.c
 * GPIO18 按键消抖与长短按判定。
 *
 * - 按下为低电平（上拉输入）
 * - 按住 >= 3000ms：长按，释放时不产生单击
 * - 按下 50ms~3000ms 后释放：单击
 */
#include "slecg_button.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "freertos/task.h"

#define SLECG_BTN_TASK_STACK        2048
#define SLECG_BTN_TASK_PRIO         4
#define SLECG_BTN_POLL_MS           10
#define SLECG_BTN_DEBOUNCE_MS       50
#define SLECG_BTN_LONG_MS           3000

static QueueHandle_t s_event_queue;

static bool btn_is_pressed(void)
{
    return gpio_get_level(BOARD_BTN_GPIO) == 0;
}

static void btn_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void post_event(slecg_event_id_t id)
{
    slecg_event_t evt = { .id = id };
    xQueueSend(s_event_queue, &evt, 0);
}

static void btn_task(void *arg)
{
    bool stable_pressed = false;
    bool long_fired = false;
    uint32_t press_ms = 0;

    (void)arg;

    while (true) {
        bool raw = btn_is_pressed();

        if (raw) {
            if (press_ms < 60000U) {
                press_ms += SLECG_BTN_POLL_MS;
            }
            if (!stable_pressed && press_ms >= SLECG_BTN_DEBOUNCE_MS) {
                stable_pressed = true;
            }
            if (stable_pressed && !long_fired && press_ms >= SLECG_BTN_LONG_MS) {
                long_fired = true;
                post_event(SLECG_EVT_BTN_LONG);
            }
        } else {
            if (stable_pressed && !long_fired && press_ms >= SLECG_BTN_DEBOUNCE_MS) {
                post_event(SLECG_EVT_BTN_SHORT);
            }
            stable_pressed = false;
            long_fired = false;
            press_ms = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(SLECG_BTN_POLL_MS));
    }
}

void slecg_button_start(QueueHandle_t event_queue)
{
    s_event_queue = event_queue;
    btn_gpio_init();
    xTaskCreate(btn_task, "slecg_btn", SLECG_BTN_TASK_STACK, NULL, SLECG_BTN_TASK_PRIO, NULL);
}
