/*
 * slecg_uart_stream.h
 * UART0 二进制数据输出与日志开关。
 */
#ifndef SLECG_UART_STREAM_H
#define SLECG_UART_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief 进入二进制流前调用：关 CRLF、stdout 无缓冲 */
void slecg_uart_stream_prepare_binary(void);

/** @brief 向 UART0 写入原始帧（经 stdout/VFS，与 ESP_LOG 同通道） */
esp_err_t slecg_uart_stream_write(const uint8_t *data, size_t len);

/** @brief 排空尚未写出的文本（进入采集前调用） */
void slecg_uart_stream_flush(void);

/** @brief 关闭 UART0 日志输出（采集时避免污染数据流） */
void slecg_uart_stream_logs_disable(void);

/** @brief 恢复 UART0 日志输出 */
void slecg_uart_stream_logs_enable(void);

#endif /* SLECG_UART_STREAM_H */
