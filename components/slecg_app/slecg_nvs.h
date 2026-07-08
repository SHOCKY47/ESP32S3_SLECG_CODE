/*
 * slecg_nvs.h
 * 传输模式 NVS 持久化。
 */
#ifndef SLECG_NVS_H
#define SLECG_NVS_H

#include "esp_err.h"
#include "slecg_state.h"

/** @brief 从 NVS 读取传输模式，无效时返回 SLECG_TRANSPORT_UART */
esp_err_t slecg_nvs_load_transport(slecg_transport_mode_t *mode);

/** @brief 保存传输模式到 NVS */
esp_err_t slecg_nvs_save_transport(slecg_transport_mode_t mode);

#endif /* SLECG_NVS_H */
