#include <stdbool.h>

#include "ads129x.h"
#include "ble_slecg.h"
#include "esp_err.h"
#include "esp_log.h"
#include "slecg_app.h"

static const char *TAG = "main";

void app_main(void)
{
	int ret;
	esp_err_t ble_ret;
	bool ads_ok = false;

	ESP_LOGI(TAG, "ESP32S3 SL-ECG starting...");

	ble_ret = ble_slecg_init();
	if (ble_ret == ESP_OK) {
		ESP_LOGI(TAG, "BLE initialized, advertising as %s", BLE_SLECG_DEVICE_NAME);
	} else {
		ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ble_ret));
	}

	ESP_LOGI(TAG, "Initializing ADS1291...");
	ret = ads129x_init();
	if (ret == 0) {
		ESP_LOGI(TAG, "ADS1291 init OK");
		ads_ok = true;
	} else {
		ESP_LOGE(TAG, "ADS1291 init failed: %d", ret);
	}

	slecg_app_start(ads_ok);
}
