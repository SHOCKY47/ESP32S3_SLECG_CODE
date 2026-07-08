#ifndef BLE_SLECG_H
#define BLE_SLECG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#define BLE_SLECG_DEVICE_NAME       "ESP_SLECG"

/*
 * SLECG 自定义 GATT 服务（16-bit UUID，便于上位机对接）：
 * - Service 0xFFE0
 * - CMD_RX 0xFFE1：上位机写入指令（Write / Write Without Response）
 * - CMD_TX 0xFFE2：设备向上位机 Notify 响应（需写 CCCD 开启）
 */
#define BLE_SLECG_SVC_UUID          0xFFE0
#define BLE_SLECG_CHAR_CMD_RX_UUID  0xFFE1
#define BLE_SLECG_CHAR_CMD_TX_UUID  0xFFE2

#define BLE_SLECG_CMD_MAX_LEN       512
#define BLE_SLECG_TX_MAX_LEN        512

/** @brief GATT CMD_RX 收到原始字节时的回调（在 BLE 任务上下文，需快速返回） */
typedef void (*ble_slecg_rx_cb_t)(const uint8_t *data, uint16_t len, void *ctx);

esp_err_t ble_slecg_init(void);
esp_err_t ble_slecg_send_notify(const uint8_t *data, size_t len);

/** @brief 注册下行数据回调，ctx 原样传回 */
void ble_slecg_set_rx_handler(ble_slecg_rx_cb_t cb, void *ctx);

bool ble_slecg_is_connected(void);
bool ble_slecg_is_notify_enabled(void);

#endif /* BLE_SLECG_H */
