/*
 * slecg_nvs.c
 * 传输模式 NVS 持久化实现。
 */
#include "slecg_nvs.h"

#include "nvs.h"
#include "nvs_flash.h"

#define SLECG_NVS_NAMESPACE     "slecg"
#define SLECG_NVS_KEY_TRANSPORT "transport"

esp_err_t slecg_nvs_load_transport(slecg_transport_mode_t *mode)
{
    nvs_handle_t handle;
    esp_err_t err;
    uint8_t value;

    if (mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(SLECG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        *mode = SLECG_TRANSPORT_UART;
        return err;
    }

    err = nvs_get_u8(handle, SLECG_NVS_KEY_TRANSPORT, &value);
    nvs_close(handle);

    if (err != ESP_OK) {
        *mode = SLECG_TRANSPORT_UART;
        return err;
    }

    if (value > (uint8_t)SLECG_TRANSPORT_BLE) {
        *mode = SLECG_TRANSPORT_UART;
        return ESP_ERR_INVALID_RESPONSE;
    }

    *mode = (slecg_transport_mode_t)value;
    return ESP_OK;
}

esp_err_t slecg_nvs_save_transport(slecg_transport_mode_t mode)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (mode > SLECG_TRANSPORT_BLE) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(SLECG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, SLECG_NVS_KEY_TRANSPORT, (uint8_t)mode);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
