/*
 * slecg_transport.c
 * 传输路由实现。
 */
#include "slecg_transport.h"

#include "ble_slecg.h"
#include "slecg_uart_stream.h"

esp_err_t slecg_transport_send_data(const slecg_runtime_t *runtime,
                                     const uint8_t *frame, size_t len)
{
    if (runtime == NULL || frame == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (runtime->transport == SLECG_TRANSPORT_UART) {
        return slecg_uart_stream_write(frame, len);
    }
    return ble_slecg_send_notify(frame, len);
}

esp_err_t slecg_transport_send_ble_ctrl(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return ble_slecg_send_notify(frame, len);
}
