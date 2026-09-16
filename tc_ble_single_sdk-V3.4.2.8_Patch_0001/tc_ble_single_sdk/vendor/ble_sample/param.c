#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "param.h"
#include "bms_error.h"
#include "bms_sw_protection.h"
#include "bms_cold_kv_store.h"
#include "bms_event_log.h"
#include "d008_product_profile.h"
#include "soc_kv_store.h"
#include "runtime.h"
#include <string.h>

#include "bms_config_store.h"
#include "bms_state_store.h"

PARAM_T g_tParam;
static uint8_t s_protection_params_valid;
static uint8_t s_storage_upgrade_valid;

uint8_t bms_protection_params_valid(void)
{
    return s_protection_params_valid && s_storage_upgrade_valid;
}

static void param_fill_default(PARAM_T *param)
{
    param->ParamVer = PARAM_VER;
    bms_cold_kv_store_get_default_protect(&param->protect);
}

void LoadParam(void)
{
#if defined(PARAM_SAVE_TO_EEPROM)
#error "bms_cold_kv_store currently supports Flash-backed param storage only"
#endif

    s_protection_params_valid = 0u;

    if (!bms_cold_kv_store_init()) {
        param_fill_default(&g_tParam);
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }


    g_tParam.ParamVer = PARAM_VER;
    if (!bms_cold_kv_store_get_protect(&g_tParam.protect)) {
        param_fill_default(&g_tParam);
        if (!bms_cold_kv_store_set_protect(&g_tParam.protect)) {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
            return;
        }
    }

    /* Communication writes are validated before SaveParam(). Validate loaded
     * Flash data as well, but do not replace unrelated customer parameters with
     * defaults merely because an old/corrupt record is detected. */
    if (!bms_sw_protection_validate_params(&g_tParam.protect)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }
    s_protection_params_valid = 1u;
}

uint8_t SaveParam(void)
{
    if (!bms_sw_protection_validate_params(&g_tParam.protect)) {
        s_protection_params_valid = 0u;
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    g_tParam.ParamVer = PARAM_VER;
    if (!bms_cold_kv_store_set_protect(&g_tParam.protect)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    s_protection_params_valid = 1u;
    return 1u;
}

void Param_UpgradeReset_Apply(void)
{
    /* A failed boot update remains inhibited until reboot/retry through this
     * startup path. A later communication SaveParam cannot clear this gate. */
    s_storage_upgrade_valid = 0u;
    if (!bms_config_store_apply_revisions() || !bms_state_store_init() ||
        !bms_event_log_init()) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }
    s_storage_upgrade_valid = 1u;
}
