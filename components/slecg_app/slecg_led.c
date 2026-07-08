/*
 * slecg_led.c
 * 双色 LED 指示实现。
 *
 * 规则：
 * - 绿色 = UART 模式，蓝色 = BLE 模式；非当前模式灯熄灭
 * - ACQ_IDLE：当前模式灯常亮
 * - ACQ_RUNNING：当前模式灯每 1.5s 翻转
 * - 错误提示：覆盖正常逻辑，200ms 快闪
 */
#include "slecg_led.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/task.h"

#define SLECG_LED_TASK_STACK        2048
#define SLECG_LED_TASK_PRIO         3
#define SLECG_LED_POLL_MS           50
#define SLECG_LED_RUN_TOGGLE_US     1500000LL
#define SLECG_LED_ERR_FLASH_MS      200
#define SLECG_LED_ERR_FLASH_COUNT   6

static const slecg_runtime_t *s_runtime;
static volatile int s_err_flash_remaining;

static void led_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_LED_GREEN_GPIO) | (1ULL << BOARD_LED_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    BOARD_LED_OFF(BOARD_LED_GREEN_GPIO);
    BOARD_LED_OFF(BOARD_LED_BLUE_GPIO);
}

void slecg_led_show_error_hint(void)
{
    s_err_flash_remaining = SLECG_LED_ERR_FLASH_COUNT;
}

static void apply_led_levels(bool green_on, bool blue_on)
{
    if (green_on) {
        BOARD_LED_ON(BOARD_LED_GREEN_GPIO);
    } else {
        BOARD_LED_OFF(BOARD_LED_GREEN_GPIO);
    }
    if (blue_on) {
        BOARD_LED_ON(BOARD_LED_BLUE_GPIO);
    } else {
        BOARD_LED_OFF(BOARD_LED_BLUE_GPIO);
    }
}

static void led_task(void *arg)
{
    bool err_phase_on = false;
    int64_t err_next_us = 0;
    bool run_phase_on = false;
    int64_t run_next_us = 0;

    (void)arg;

    while (true) {
        bool green_on = false;
        bool blue_on = false;

        if (s_err_flash_remaining > 0) {
            int64_t now = esp_timer_get_time();
            if (now >= err_next_us) {
                err_phase_on = !err_phase_on;
                err_next_us = now + (int64_t)SLECG_LED_ERR_FLASH_MS * 1000LL;
                s_err_flash_remaining--;
            }
            if (s_runtime->transport == SLECG_TRANSPORT_UART) {
                green_on = err_phase_on;
            } else {
                blue_on = err_phase_on;
            }
        } else if (s_runtime != NULL) {
            int64_t now = esp_timer_get_time();
            bool active_on = true;

            if (s_runtime->acq == SLECG_ACQ_RUNNING) {
                if (now >= run_next_us) {
                    run_phase_on = !run_phase_on;
                    run_next_us = now + SLECG_LED_RUN_TOGGLE_US;
                }
                active_on = run_phase_on;
            } else {
                run_phase_on = true;
                run_next_us = now;
                active_on = true;
            }

            if (s_runtime->transport == SLECG_TRANSPORT_UART) {
                green_on = active_on;
            } else {
                blue_on = active_on;
            }
        }

        apply_led_levels(green_on, blue_on);
        vTaskDelay(pdMS_TO_TICKS(SLECG_LED_POLL_MS));
    }
}

void slecg_led_start(const slecg_runtime_t *runtime)
{
    s_runtime = runtime;
    led_gpio_init();
    xTaskCreate(led_task, "slecg_led", SLECG_LED_TASK_STACK, NULL, SLECG_LED_TASK_PRIO, NULL);
}
