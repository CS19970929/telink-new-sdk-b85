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
static int g_param_trusted;

#define BMS_PROTECT_STRUCT_VERSION 0x0101u

static u32 param_crc32(const u8 *data, u32 len)
{
    u32 crc = 0xFFFFFFFFu;
    u32 i;
    u8 bit;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

void Param_PrepareProtect(struct PRT_E2ROM_PARAS *protect)
{
    if (protect == NULL) return;
    protect->struct_version = BMS_PROTECT_STRUCT_VERSION;
    protect->struct_length = (u16)sizeof(*protect);
    protect->struct_crc32 = 0u;
    protect->struct_crc32 = param_crc32((const u8 *)protect, sizeof(*protect));
}

int Param_ValidateProtect(const struct PRT_E2ROM_PARAS *protect)
{
    struct PRT_E2ROM_PARAS copy;
    u32 expected_crc;
    const u16 *values;
    u16 i;

    if (protect == NULL ||
        protect->struct_version != BMS_PROTECT_STRUCT_VERSION ||
        protect->struct_length != sizeof(*protect)) return 0;
    copy = *protect;
    expected_crc = copy.struct_crc32;
    copy.struct_crc32 = 0u;
    if (param_crc32((const u8 *)&copy, sizeof(copy)) != expected_crc) return 0;

    if (protect->u16VcellOvp_Rcv >= protect->u16VcellOvp_First ||
        protect->u16VcellUvp_Rcv <= protect->u16VcellUvp_First ||
        protect->u16VbusOvp_Rcv >= protect->u16VbusOvp_Third ||
        protect->u16VbusUvp_Rcv <= protect->u16VbusUvp_Third) return 0;
    if (protect->u16IdsgOcp_Second <= protect->u16IdsgOcp_First ||
        protect->u16IdsgOcp_Third <= protect->u16IdsgOcp_Second ||
        protect->u16IchgOcp_Second <= protect->u16IchgOcp_First ||
        protect->u16IchgOcp_Third <= protect->u16IchgOcp_Second) return 0;
    if ((u32)protect->u16IdsgOcp_Third >= ((u32)AFE_ODC2 * 10u)) return 0;
    if (protect->u16TChgOTp_Rcv >= protect->u16TChgOTp_Third ||
        protect->u16TchgUTp_Rcv <= protect->u16TchgUTp_Third ||
        protect->u16TdischgOTp_Rcv >= protect->u16TdischgOTp_Third ||
        protect->u16TdischgUTp_Rcv <= protect->u16TdischgUTp_Third ||
        protect->u16TmosOTp_Rcv >= protect->u16TmosOTp_Third) return 0;
    if (protect->u16VcellUvp_First < 1000u || protect->u16VcellOvp_Third > 5000u ||
        protect->u16VcellUvp_First >= protect->u16VcellOvp_First) return 0;
    if (protect->u16VbusUvp_First >= protect->u16VbusOvp_First) return 0;

    values = &protect->u16VcellOvp_First;
    for (i = 4u; i < 65u; i = (u16)(i + 5u)) {
        if (values[i] == 0u || values[i] > 60000u) return 0;
    }
    return 1;
}

int Param_CommitProtect(const struct PRT_E2ROM_PARAS *protect)
{
    struct PRT_E2ROM_PARAS candidate;
    if (protect == NULL) return 0;
    candidate = *protect;
    Param_PrepareProtect(&candidate);
    if (!Param_ValidateProtect(&candidate)) return 0;
    if (!bms_cold_kv_store_set_protect(&candidate)) return 0;
    g_tParam.protect = candidate;
    g_tParam.ParamVer = PARAM_VER;
    g_param_trusted = 1;
    return 1;
}

int Param_IsTrusted(void)
{
    return g_param_trusted;
}

static void param_fill_default(PARAM_T *param)
{
    param->ParamVer = PARAM_VER;
    bms_cold_kv_store_get_default_protect(&param->protect);
    Param_PrepareProtect(&param->protect);
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

    g_param_trusted = 0;
    if (!bms_cold_kv_store_init()) {
        param_fill_default(&g_tParam);
        return;
    }

    g_tParam.ParamVer = PARAM_VER;
    if (!bms_cold_kv_store_get_protect(&g_tParam.protect) ||
        !Param_ValidateProtect(&g_tParam.protect)) {
        param_fill_default(&g_tParam);
        g_param_trusted = bms_cold_kv_store_set_protect(&g_tParam.protect);
        return;
    }
    g_param_trusted = 1;
}

void SaveParam(void)
{
    if (!Param_CommitProtect(&g_tParam.protect)) {
        g_param_trusted = 0;
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
