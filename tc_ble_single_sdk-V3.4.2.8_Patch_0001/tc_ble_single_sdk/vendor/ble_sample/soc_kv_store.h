#pragma once

/* Transitional source-compatibility facade. Persistence is owned by
 * bms_state_store.*; there is no SOC KV database in Storage V1. */
#include "bms_state_store.h"

typedef bms_state_store_data_t soc_kv_data_t;

#define SOC_KV_FLAG_CAPACITY_LEARNED      BMS_STATE_FLAG_CAPACITY_LEARNED
#define SOC_PARAM_DEFAULT_SOC              BMS_STATE_DEFAULT_SOC
#define SOC_PARAM_DEFAULT_DSG              BMS_STATE_DEFAULT_DSG
#define SOC_PARAM_DEFAULT_CYCLE            BMS_STATE_DEFAULT_CYCLE
#define SOC_PARAM_DEFAULT_LEARNED_CAPACITY BMS_STATE_DEFAULT_LEARNED_CAPACITY
#define SOC_PARAM_DEFAULT_FLAGS            BMS_STATE_DEFAULT_FLAGS

#define soc_kv_store_init                  bms_state_store_init
#define soc_kv_store_get                   bms_state_store_get
#define soc_kv_store_get_default_data      bms_state_store_get_default_data
#define soc_kv_store_write_all             bms_state_store_write_all
#define soc_kv_store_write_learning        bms_state_store_write_learning
#define soc_kv_store_update_and_log_if_changed bms_state_store_update_and_log_if_changed
#define soc_kv_store_get_runtime_min       bms_state_store_get_runtime_min
#define soc_kv_store_write_runtime_min     bms_state_store_write_runtime_min
#define soc_kv_store_reset_runtime         bms_state_store_reset_runtime
