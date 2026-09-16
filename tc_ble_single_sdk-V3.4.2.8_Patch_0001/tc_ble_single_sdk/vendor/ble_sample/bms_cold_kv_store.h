#pragma once

/* Transitional source-compatibility facade. Persistence is owned by
 * bms_config_store.*; there is no cold-KV database in Storage V1. */
#include "bms_config_store.h"

typedef bms_config_system_param_id_t bms_cold_system_param_id_t;
typedef bms_config_control_param_id_t bms_cold_control_param_id_t;
typedef bms_config_system_params_t bms_cold_system_params_t;

#define BMS_COLD_CTRL_PROTECT_RESET_EPOCH   BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH
#define BMS_COLD_CTRL_SYSTEM_RESET_EPOCH    BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH
#define BMS_COLD_CTRL_SOC_RESET_EPOCH       BMS_CONFIG_CTRL_SOC_RESET_EPOCH
#define BMS_COLD_CTRL_EVENT_LOG_RESET_EPOCH BMS_CONFIG_CTRL_EVENT_LOG_RESET_EPOCH
#define BMS_COLD_CTRL_RUNTIME_RESET_EPOCH   BMS_CONFIG_CTRL_RUNTIME_RESET_EPOCH
#define BMS_COLD_CTRL_COUNT                 BMS_CONFIG_CTRL_COUNT

#define bms_cold_kv_store_init               bms_config_store_init
#define bms_cold_kv_store_get_protect        bms_config_store_get_protect
#define bms_cold_kv_store_set_protect        bms_config_store_set_protect
#define bms_cold_kv_store_get_system         bms_config_store_get_system
#define bms_cold_kv_store_set_system         bms_config_store_set_system
#define bms_cold_kv_store_get_afe_hw_profile bms_config_store_get_afe_hw_profile
#define bms_cold_kv_store_set_afe_hw_profile bms_config_store_set_afe_hw_profile
#define bms_cold_kv_store_get_control_value  bms_config_store_get_control_value
#define bms_cold_kv_store_set_control_value  bms_config_store_set_control_value
#define bms_cold_kv_store_get_bt_name_suffix bms_config_store_get_bt_name_suffix
#define bms_cold_kv_store_set_bt_name_suffix bms_config_store_set_bt_name_suffix
#define bms_cold_kv_store_get_default_protect bms_config_store_get_default_protect
#define bms_cold_kv_store_get_default_system  bms_config_store_get_default_system
