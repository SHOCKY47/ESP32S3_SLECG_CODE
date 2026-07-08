/*
 * slecg_transport.h
 * 按传输模式路由 ECG 等上行数据；控制类应答始终走 BLE。
 */
#ifndef SLECG_TRANSPORT_H
#define SLECG_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "slecg_state.h"

/** @brief 发送 ECG 等业务数据帧（按 transport 选择 UART 或 BLE） */
esp_err_t slecg_transport_send_data(const slecg_runtime_t *runtime,
                                     const uint8_t *frame, size_t len);

/** @brief 发送 ACK/NACK/STATUS 等控制帧（始终尝试 BLE Notify） */
esp_err_t slecg_transport_send_ble_ctrl(const uint8_t *frame, size_t len);

#endif /* SLECG_TRANSPORT_H */
