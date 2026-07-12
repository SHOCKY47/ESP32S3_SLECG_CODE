/*
 * slecg_uart_stream.c
 * UART0 数据流与运行时日志控制。
 *
 * 重要：上位机侧已证实 ESP_LOG 文本能到达 CH340，但 uart_write_bytes
 * 路径在采集期可能写不出去。因此 ECG 二进制统一走 stdout/VFS
 * （与 ESP_LOG 相同通道），并关闭 TX CRLF，避免 0x0A 被改写。
 */
#include "slecg_uart_stream.h"

#include <stdio.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define SLECG_UART_PORT             UART_NUM_0
#else
#define SLECG_UART_PORT             CONFIG_ESP_CONSOLE_UART_NUM
#endif

static bool s_binary_prepared;

void slecg_uart_stream_prepare_binary(void)
{
	if (s_binary_prepared) {
		return;
	}
	uart_vfs_dev_port_set_tx_line_endings(SLECG_UART_PORT, ESP_LINE_ENDINGS_LF);
	setvbuf(stdout, NULL, _IONBF, 0); /* 无缓冲，避免二进制卡住 */
	s_binary_prepared = true;
}

esp_err_t slecg_uart_stream_write(const uint8_t *data, size_t len)
{
	size_t written;

	if (data == NULL || len == 0) {
		return ESP_ERR_INVALID_ARG;
	}

	slecg_uart_stream_prepare_binary();

	written = fwrite(data, 1, len, stdout);
	if (written != len) {
		return ESP_FAIL;
	}
	/* _IONBF 下仍 fflush 一次，确保立刻推到驱动 */
	if (fflush(stdout) != 0) {
		return ESP_FAIL;
	}
	return ESP_OK;
}

void slecg_uart_stream_flush(void)
{
	fflush(stdout);
}

void slecg_uart_stream_logs_disable(void)
{
	esp_log_level_set("*", ESP_LOG_NONE);
}

void slecg_uart_stream_logs_enable(void)
{
	esp_log_level_set("*", ESP_LOG_INFO);
}
