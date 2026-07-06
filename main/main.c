#include <stdbool.h>

#include "ads129x.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void board_led_init(void)
{
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << BOARD_LED_GREEN_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_ERROR_CHECK(gpio_config(&cfg));
	BOARD_LED_OFF(BOARD_LED_GREEN_GPIO);
}

void app_main(void)
{
	int ret;
	bool healthy = false;

	board_led_init();

	ESP_LOGI(TAG, "ESP32S3 SL-ECG starting...");
	ESP_LOGI(TAG, "Initializing ADS1291 (no acquisition yet)");

	ret = ads129x_init();
	if (ret == 0) {
		ESP_LOGI(TAG, "ADS1291 init OK");
		healthy = true;
	} else {
		ESP_LOGE(TAG, "ADS1291 init failed: %d", ret);
	}

	while (true) {
		if (healthy) {
			static bool led_on = false;

			led_on = !led_on;
			if (led_on) {
				BOARD_LED_ON(BOARD_LED_GREEN_GPIO);
			} else {
				BOARD_LED_OFF(BOARD_LED_GREEN_GPIO);
			}
		}

		vTaskDelay(pdMS_TO_TICKS(500));
	}
}
