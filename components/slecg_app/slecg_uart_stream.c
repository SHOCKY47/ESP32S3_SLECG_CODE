/*
 * slecg_uart_stream.c
 * UART0 数据流与运行时日志控制。
 */
#include "slecg_uart_stream.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define SLECG_UART_PORT             UART_NUM_0
#else
#define SLECG_UART_PORT             CONFIG_ESP_CONSOLE_UART_NUM
#endif

esp_err_t slecg_uart_stream_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int written = uart_write_bytes(SLECG_UART_PORT, data, len);
    if (written < 0 || (size_t)written != len) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void slecg_uart_stream_flush(void)
{
    uart_flush(SLECG_UART_PORT);
}

void slecg_uart_stream_logs_disable(void)
{
    esp_log_level_set("*", ESP_LOG_NONE);
}

void slecg_uart_stream_logs_enable(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
}
