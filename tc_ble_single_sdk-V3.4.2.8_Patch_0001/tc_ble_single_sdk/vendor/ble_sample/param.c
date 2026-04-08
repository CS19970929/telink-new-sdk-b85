#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "param.h"
#include "bms_cold_kv_store.h"
#include "bms_event_log.h"
#include "soc_kv_store.h"
#include "runtime.h"
#include "sh367309_datadeal.h"
#include <string.h>

PARAM_T g_tParam;

static void param_fill_default(PARAM_T *param)
{
    param->ParamVer = PARAM_VER;
    bms_cold_kv_store_get_default_protect(&param->protect);
}

static int param_protect_equals(const struct PRT_E2ROM_PARAS *a, const struct PRT_E2ROM_PARAS *b)
{
    return (memcmp(a, b, sizeof(*a)) == 0);
}

static int param_upgrade_epoch_mismatch(bms_cold_control_param_id_t item, u32 desired_epoch)
{
    u32 applied_epoch = 0u;

    if (desired_epoch == 0u) {
        return 0;
    }

    if (!bms_cold_kv_store_get_control_value(item, &applied_epoch)) {
        applied_epoch = 0u;
    }

    return (applied_epoch != desired_epoch);
}

static void param_upgrade_mark_epoch(bms_cold_control_param_id_t item, u32 desired_epoch)
{
    if (desired_epoch != 0u) {
        (void)bms_cold_kv_store_set_control_value(item, desired_epoch);
    }
}

static int param_upgrade_apply_default_protect(void)
{
    param_fill_default(&g_tParam);
    return bms_cold_kv_store_set_protect(&g_tParam.protect);
}

static int param_upgrade_apply_default_system(void)
{
    bms_cold_system_params_t system;

    bms_cold_kv_store_get_default_system(&system);
    return bms_cold_kv_store_set_system(&system);
}

static int param_upgrade_apply_default_soc(void)
{
    soc_kv_data_t defaults = soc_kv_store_get_default_data();

    if (!soc_kv_store_init()) {
        return 0;
    }

    /* 升级重置只需要覆盖当前值，不需要额外整区擦除。 */
    return soc_kv_store_write_all(defaults.soc, defaults.dsg, defaults.cycle);
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
    PARAM_T legacy_param;
    struct PRT_E2ROM_PARAS default_protect;

    memset(&legacy_param, 0xFF, sizeof(legacy_param));

#ifdef PARAM_SAVE_TO_FLASH
    flash_read_page(PARAM_ADDR, sizeof(PARAM_T), (u8 *)&legacy_param);
#endif

    if (!bms_cold_kv_store_init()) {
        param_fill_default(&g_tParam);
        return;
    }

    g_tParam.ParamVer = PARAM_VER;
    if (!bms_cold_kv_store_get_protect(&g_tParam.protect)) {
        param_fill_default(&g_tParam);
        (void)bms_cold_kv_store_set_protect(&g_tParam.protect);
        return;
    }

    bms_cold_kv_store_get_default_protect(&default_protect);
    if ((legacy_param.ParamVer == PARAM_VER) &&
        !param_protect_equals(&legacy_param.protect, &g_tParam.protect) &&
        param_protect_equals(&g_tParam.protect, &default_protect)) {
        g_tParam.protect = legacy_param.protect;
        (void)bms_cold_kv_store_set_protect(&g_tParam.protect);
    }
}

void SaveParam(void)
{
    g_tParam.ParamVer = PARAM_VER;
    if (!bms_cold_kv_store_set_protect(&g_tParam.protect)) {
        System_ERROR_UserCallback(ERROR_EEPROM_STORE);
    }
}

void Param_UpgradeReset_Apply(void)
{
    if (!bms_cold_kv_store_init()) {
        return;
    }

    if (param_upgrade_epoch_mismatch(BMS_COLD_CTRL_PROTECT_RESET_EPOCH, FW_UPGRADE_RESET_PROTECT_EPOCH)) {
        if (param_upgrade_apply_default_protect()) {
            param_upgrade_mark_epoch(BMS_COLD_CTRL_PROTECT_RESET_EPOCH, FW_UPGRADE_RESET_PROTECT_EPOCH);
        } else {
            System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_COLD_CTRL_SYSTEM_RESET_EPOCH, FW_UPGRADE_RESET_SYSTEM_EPOCH)) {
        if (param_upgrade_apply_default_system()) {
            param_upgrade_mark_epoch(BMS_COLD_CTRL_SYSTEM_RESET_EPOCH, FW_UPGRADE_RESET_SYSTEM_EPOCH);
        } else {
            System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_COLD_CTRL_SOC_RESET_EPOCH, FW_UPGRADE_RESET_SOC_EPOCH)) {
        if (param_upgrade_apply_default_soc()) {
            param_upgrade_mark_epoch(BMS_COLD_CTRL_SOC_RESET_EPOCH, FW_UPGRADE_RESET_SOC_EPOCH);
        } else {
            System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_COLD_CTRL_EVENT_LOG_RESET_EPOCH, FW_UPGRADE_RESET_EVENT_LOG_EPOCH)) {
        if (param_upgrade_apply_default_event_log()) {
            param_upgrade_mark_epoch(BMS_COLD_CTRL_EVENT_LOG_RESET_EPOCH, FW_UPGRADE_RESET_EVENT_LOG_EPOCH);
        } else {
            System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        }
    }

    if (param_upgrade_epoch_mismatch(BMS_COLD_CTRL_RUNTIME_RESET_EPOCH, FW_UPGRADE_RESET_RUNTIME_EPOCH)) {
        if (param_upgrade_apply_default_runtime()) {
            param_upgrade_mark_epoch(BMS_COLD_CTRL_RUNTIME_RESET_EPOCH, FW_UPGRADE_RESET_RUNTIME_EPOCH);
        } else {
            System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        }
    }
}
