#include "ble_slecg.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "ble_slecg";

enum {
    SLECG_IDX_SVC,
    SLECG_IDX_CMD_RX_CHAR,
    SLECG_IDX_CMD_RX_VAL,
    SLECG_IDX_CMD_TX_CHAR,
    SLECG_IDX_CMD_TX_VAL,
    SLECG_IDX_CMD_TX_CCCD,
    SLECG_IDX_NB,
};

#define ADV_CONFIG_FLAG       (1 << 0)
#define RAND_ADDR_CONFIG_FLAG (1 << 1)

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                       ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static const uint16_t slecg_service_uuid = BLE_SLECG_SVC_UUID;
static const uint16_t slecg_cmd_rx_uuid = BLE_SLECG_CHAR_CMD_RX_UUID;
static const uint16_t slecg_cmd_tx_uuid = BLE_SLECG_CHAR_CMD_TX_UUID;
static const uint8_t cccd_init[2] = {0x00, 0x00};

static const esp_gatts_attr_db_t gatt_db[SLECG_IDX_NB] = {
    [SLECG_IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(slecg_service_uuid), (uint8_t *)&slecg_service_uuid},
    },
    [SLECG_IDX_CMD_RX_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_write},
    },
    [SLECG_IDX_CMD_RX_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&slecg_cmd_rx_uuid,
         ESP_GATT_PERM_WRITE,
         BLE_SLECG_CMD_MAX_LEN, 0, NULL},
    },
    [SLECG_IDX_CMD_TX_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_notify},
    },
    [SLECG_IDX_CMD_TX_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&slecg_cmd_tx_uuid,
         ESP_GATT_PERM_READ,
         BLE_SLECG_TX_MAX_LEN, 0, NULL},
    },
    [SLECG_IDX_CMD_TX_CCCD] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(uint16_t), sizeof(cccd_init), (uint8_t *)cccd_init},
    },
};

static uint8_t adv_service_uuid128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint16_t slecg_handle_table[SLECG_IDX_NB];
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id = 0xFFFF;
static bool s_connected = false;
static bool s_notify_enabled = false;
static esp_bd_addr_t s_rand_addr = {0};
static uint8_t s_adv_config_done = 0;
static ble_slecg_rx_cb_t s_rx_cb = NULL;
static void *s_rx_ctx = NULL;

static void start_advertising_if_ready(void)
{
    if ((s_adv_config_done & (ADV_CONFIG_FLAG | RAND_ADDR_CONFIG_FLAG)) ==
        (ADV_CONFIG_FLAG | RAND_ADDR_CONFIG_FLAG)) {
        esp_ble_gap_start_advertising(&adv_params);
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        s_adv_config_done |= ADV_CONFIG_FLAG;
        start_advertising_if_ready();
        break;
    case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
        if (param->set_rand_addr_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_config_done |= RAND_ADDR_CONFIG_FLAG;
            ESP_LOGI(TAG, "Random static address set");
            ESP_LOG_BUFFER_HEX(TAG, s_rand_addr, ESP_BD_ADDR_LEN);
            start_advertising_if_ready();
        } else {
            ESP_LOGE(TAG, "Set random address failed, status=%d",
                     param->set_rand_addr_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed, status=%d", param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "Advertising started as \"%s\"", BLE_SLECG_DEVICE_NAME);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising stopped, status=%d", param->adv_stop_cmpl.status);
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Conn params updated: int=%d latency=%d timeout=%d",
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        esp_err_t set_name_ret = esp_ble_gap_set_device_name(BLE_SLECG_DEVICE_NAME);
        if (set_name_ret != ESP_OK) {
            ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(set_name_ret));
        }

        esp_ble_gap_addr_create_static(s_rand_addr);
        esp_ble_gap_set_rand_addr(s_rand_addr);

        esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Config adv data failed: %s", esp_err_to_name(ret));
        }

        esp_err_t create_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, SLECG_IDX_NB, 0);
        if (create_ret != ESP_OK) {
            ESP_LOGE(TAG, "Create attr table failed: %s", esp_err_to_name(create_ret));
        }
        break;
    }
    case ESP_GATTS_READ_EVT:
        ESP_LOGD(TAG, "GATT read, handle=%u", param->read.handle);
        break;
    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            if (slecg_handle_table[SLECG_IDX_CMD_RX_VAL] == param->write.handle) {
                if (s_rx_cb != NULL && param->write.len > 0) {
                    s_rx_cb(param->write.value, param->write.len, s_rx_ctx);
                }
            } else if (slecg_handle_table[SLECG_IDX_CMD_TX_CCCD] == param->write.handle &&
                       param->write.len == 2) {
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                s_notify_enabled = (descr_value == 0x0001);
                ESP_LOGI(TAG, "Notify %s", s_notify_enabled ? "enabled" : "disabled");
            }

            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                            ESP_GATT_OK, NULL);
            }
        }
        break;
    case ESP_GATTS_CONNECT_EVT:
        s_connected = true;
        s_conn_id = param->connect.conn_id;
        s_gatts_if = gatts_if;
        ESP_LOGI(TAG, "Client connected, conn_id=%u", s_conn_id);
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;
        conn_params.min_int = 0x10;
        conn_params.timeout = 400;
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        s_notify_enabled = false;
        s_conn_id = 0xFFFF;
        ESP_LOGI(TAG, "Client disconnected, reason=0x%02x", param->disconnect.reason);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Create attribute table failed, error=0x%x",
                     param->add_attr_tab.status);
            break;
        }
        if (param->add_attr_tab.num_handle != SLECG_IDX_NB) {
            ESP_LOGE(TAG, "Unexpected handle count: %d", param->add_attr_tab.num_handle);
            break;
        }

        memcpy(slecg_handle_table, param->add_attr_tab.handles, sizeof(slecg_handle_table));
        esp_ble_gatts_start_service(slecg_handle_table[SLECG_IDX_SVC]);
        ESP_LOGI(TAG, "GATT service started, RX handle=%u TX handle=%u",
                 slecg_handle_table[SLECG_IDX_CMD_RX_VAL],
                 slecg_handle_table[SLECG_IDX_CMD_TX_VAL]);
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            s_gatts_if = gatts_if;
        } else {
            ESP_LOGE(TAG, "GATT register failed, status=0x%x", param->reg.status);
            return;
        }
    }

    if (gatts_if == ESP_GATT_IF_NONE || gatts_if == s_gatts_if) {
        gatts_profile_event_handler(event, gatts_if, param);
    }
}

void ble_slecg_set_rx_handler(ble_slecg_rx_cb_t cb, void *ctx)
{
    s_rx_cb = cb;
    s_rx_ctx = ctx;
}

bool ble_slecg_is_connected(void)
{
    return s_connected;
}

bool ble_slecg_is_notify_enabled(void)
{
    return s_notify_enabled;
}

esp_err_t ble_slecg_send_notify(const uint8_t *data, size_t len)
{
    if (!s_connected || !s_notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > BLE_SLECG_TX_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_ble_gatts_send_indicate(
        s_gatts_if, s_conn_id, slecg_handle_table[SLECG_IDX_CMD_TX_VAL],
        len, (uint8_t *)data, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send notify failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ble_slecg_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatt_set_local_mtu(517);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set local MTU failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "BLE stack initialized");
    return ESP_OK;
}
