#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "param.h"
#include "bms_error.h"
#include "bms_sw_protection.h"
#include "bms_config_store.h"
#include "bms_event_log.h"
#include "bms_state_store.h"
#include "runtime.h"
#include <string.h>

PARAM_T g_tParam;
static uint8_t s_protection_params_valid;

uint8_t bms_protection_params_valid(void)
{
    return s_protection_params_valid;
}

static void param_fill_default(PARAM_T *param)
{
    param->ParamVer = PARAM_VER;
    bms_config_store_get_default_protect(&param->protect);
}

static int param_upgrade_epoch_mismatch(bms_config_control_param_id_t item, u32 desired_epoch)
{
    u32 applied_epoch = 0u;

    if (desired_epoch == 0u) {
        return 0;
    }

    if (!bms_config_store_get_control_value(item, &applied_epoch)) {
        applied_epoch = 0u;
    }

    return (applied_epoch != desired_epoch);
}

static void param_upgrade_mark_epoch(bms_config_control_param_id_t item, u32 desired_epoch)
{
    if (desired_epoch != 0u) {
        (void)bms_config_store_set_control_value(item, desired_epoch);
    }
}

static int param_upgrade_apply_default_protect(void)
{
    param_fill_default(&g_tParam);
    if (!bms_sw_protection_validate_params(&g_tParam.protect)) return 0;
    return bms_config_store_set_protect(&g_tParam.protect);
}

static int param_upgrade_apply_default_system(void)
{
    bms_config_system_params_t system;

    bms_config_store_get_default_system(&system);
    return bms_config_store_set_system(&system);
}

static int param_upgrade_apply_default_soc(void)
{
    bms_state_store_data_t defaults = bms_state_store_get_default_data();

    if (!bms_state_store_init()) {
        return 0;
    }

    /* 升级重置只需要覆盖当前值，不需要额外整区擦除。 */
    return bms_state_store_write_all(defaults.soc, defaults.dsg, defaults.cycle);
}

static int param_upgrade_apply_default_event_log(void)
{
    return bms_event_log_factory_reset();
}

static int param_upgrade_apply_default_runtime(void)
{
    return Runtime_FactoryReset();
}

void LoadParam(void)
{
#if defined(PARAM_SAVE_TO_EEPROM)
#error "bms_cold_kv_store currently supports Flash-backed param storage only"
#endif

    s_protection_params_valid = 0u;
    if (!bms_config_store_init()) {
        param_fill_default(&g_tParam);
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }

    g_tParam.ParamVer = PARAM_VER;
    if (!bms_config_store_get_protect(&g_tParam.protect)) {
        param_fill_default(&g_tParam);
        if (!bms_config_store_set_protect(&g_tParam.protect)) {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
            return;
        }
    }
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
    if (!bms_config_store_set_protect(&g_tParam.protect)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    s_protection_params_valid = 1u;
    return 1u;
}

void Param_UpgradeReset_Apply(void)
{
    if (!bms_config_store_init()) {
        s_protection_params_valid = 0u;
        return;
    }

    if (param_upgrade_epoch_mismatch(BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH, FW_UPGRADE_RESET_PROTECT_EPOCH)) {
        if (param_upgrade_apply_default_protect()) {
            s_protection_params_valid = 1u;
            param_upgrade_mark_epoch(BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH, FW_UPGRADE_RESET_PROTECT_EPOCH);
        } else {
            s_protection_params_valid = 0u;
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH, FW_UPGRADE_RESET_SYSTEM_EPOCH)) {
        if (param_upgrade_apply_default_system()) {
            param_upgrade_mark_epoch(BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH, FW_UPGRADE_RESET_SYSTEM_EPOCH);
        } else {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_CONFIG_CTRL_SOC_RESET_EPOCH, FW_UPGRADE_RESET_SOC_EPOCH)) {
        if (param_upgrade_apply_default_soc()) {
            param_upgrade_mark_epoch(BMS_CONFIG_CTRL_SOC_RESET_EPOCH, FW_UPGRADE_RESET_SOC_EPOCH);
        } else {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_CONFIG_CTRL_EVENT_LOG_RESET_EPOCH, FW_UPGRADE_RESET_EVENT_LOG_EPOCH)) {
        if (param_upgrade_apply_default_event_log()) {
            param_upgrade_mark_epoch(BMS_CONFIG_CTRL_EVENT_LOG_RESET_EPOCH, FW_UPGRADE_RESET_EVENT_LOG_EPOCH);
        } else {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_CONFIG_CTRL_RUNTIME_RESET_EPOCH, FW_UPGRADE_RESET_RUNTIME_EPOCH)) {
        if (param_upgrade_apply_default_runtime()) {
            param_upgrade_mark_epoch(BMS_CONFIG_CTRL_RUNTIME_RESET_EPOCH, FW_UPGRADE_RESET_RUNTIME_EPOCH);
        } else {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        }
    }
}
