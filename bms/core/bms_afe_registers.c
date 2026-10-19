#include "bms_afe_registers.h"
#include "bms_afe.h"
#if BMS_AFE_REGISTER_DEBUG_ENABLE || BMS_AFE_REGISTER_PRESET_COUNT
#include "bms_afe_register_policy.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124.h"
#define REGISTER_FIRST 0u
#define REGISTER_LAST 0x8Fu /* 0x90 起始地址原文存在矛盾，目录显示但不尝试读取。 */
#else
#include "sh3673520.h"
#define REGISTER_FIRST 0x40u
#define REGISTER_LAST 0x99u
#endif

static uint8_t register_read(uint8_t reg, uint8_t *value)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return DVC1124_ReadRegisters(reg, value, 1u);
#else
    return SH3673520_ReadReg(reg, value) == SH3673520_OK;
#endif
}

static uint8_t register_write(uint8_t reg, uint8_t value)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return DVC1124_WriteRegisters(reg, &value, 1u);
#else
    return SH3673520_WriteReg(reg, value) == SH3673520_OK;
#endif
}

/* 对稳定 RW 字段执行 CAS、保留位保持及写后读回；从不自动重试上层事务。 */
static uint8_t register_update(uint8_t reg, uint8_t mask, uint8_t expected,
                               uint8_t value, uint8_t compare, uint8_t *actual)
{
    uint8_t previous, target;
    if (!register_read(reg, &previous)) return 7u;
    if (compare && previous != expected) return 6u;
    target = (uint8_t)((previous & (uint8_t)~mask) | (value & mask));
    if (target != previous && !register_write(reg, target)) return 7u;
    if (!register_read(reg, actual)) return 7u;
    return ((*actual & mask) == (target & mask)) ? 0u : 7u;
}

#if BMS_AFE_REGISTER_PRESET_COUNT
typedef struct { uint8_t reg, mask, value; } register_setting_t;
#define REGISTER_SETTING(reg, mask, value) {reg, mask, value},
static const register_setting_t s_source_settings[] = {
    BMS_AFE_REGISTER_PRESETS(REGISTER_SETTING)
    {0u, 0u, 0u}
};
#undef REGISTER_SETTING
typedef char register_setting_count_matches[
    sizeof(s_source_settings) / sizeof(s_source_settings[0]) == BMS_AFE_REGISTER_PRESET_COUNT + 1u ? 1 : -1];

uint8_t bms_afe_register_config_apply(void)
{
    uint16_t i;
    uint8_t actual;
    for (i = 0u; i + 1u < sizeof(s_source_settings) / sizeof(s_source_settings[0]); ++i) {
        const register_setting_t *setting = &s_source_settings[i];
        if ((uint16_t)(setting->reg - REGISTER_FIRST) > REGISTER_LAST - REGISTER_FIRST || !setting->mask ||
            (setting->value & (uint8_t)~setting->mask) ||
            (setting->mask & (uint8_t)~s_register_source_masks[setting->reg - REGISTER_FIRST])) return 0u;
        if (register_update(setting->reg, setting->mask, 0u, setting->value, 0u, &actual)) return 0u;
    }
    return 1u;
}
#endif

#if BMS_AFE_REGISTER_DEBUG_ENABLE
uint8_t bms_afe_register_command(const uint8_t *request, uint32_t length,
                               uint8_t *payload, uint8_t *payload_length)
{
    if (request == 0 || length < 5u || payload == 0 || payload_length == 0) return 1u;
    *payload_length = 0u;
    if (request[2] == 0x10u) {
        uint16_t model, element;
        uint8_t parallel;
        if (length != 5u) return 1u;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
        model = 0x1124u; element = DVC1124_DEFAULT_SHUNT_UOHM; parallel = 1u;
#else
        model = (BMS_BUILD_AFE_MODEL == 3520u) ? 0x3520u : 0x3510u;
        element = SH3673510_BOARD_SHUNT_ELEMENT_UOHM;
        parallel = SH3673510_BOARD_SHUNT_PARALLEL_COUNT;
#endif
        payload[0] = 1u;
        payload[1] = (uint8_t)(model >> 8); payload[2] = (uint8_t)model;
        payload[3] = BMS_AFE_REGISTER_DEBUG_ENABLE;
        payload[4] = bms_afe_register_debug_held();
        payload[5] = (uint8_t)(BMS_AFE_REGISTER_CATALOG_ID >> 24);
        payload[6] = (uint8_t)(BMS_AFE_REGISTER_CATALOG_ID >> 16);
        payload[7] = (uint8_t)(BMS_AFE_REGISTER_CATALOG_ID >> 8);
        payload[8] = (uint8_t)BMS_AFE_REGISTER_CATALOG_ID;
        payload[9] = (uint8_t)(element >> 8); payload[10] = (uint8_t)element;
        payload[11] = parallel;
        *payload_length = 12u;
        return 0u;
    }
#if BMS_AFE_REGISTER_DEBUG_ENABLE
    if (request[2] == 0x12u && length == 7u)
        return bms_afe_register_debug_begin() ? 0u : 8u;
    if (request[2] == 0x14u && length == 7u)
        return bms_afe_register_debug_restore() ? 0u : 7u;
    if (request[2] == 0x11u || request[2] == 0x13u) {
        uint8_t reg, actual, mask;
        if (length != (request[2] == 0x11u ? 9u : 11u)) return 1u;
        reg = request[5];
        if ((uint16_t)(reg - REGISTER_FIRST) > REGISTER_LAST - REGISTER_FIRST) return 5u;
        if (!bms_afe_register_bus_access_allowed()) return 7u;
        if (request[2] == 0x11u) {
            uint8_t destructive;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
            destructive = DVC1124_RegReadHasSideEffect(reg);
#else
            destructive = reg == 0x59u;
#endif
            if (request[6] > 1u || (destructive &&
                (!request[6] || !bms_afe_register_debug_held()))) return 5u;
            if (destructive) {
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
                if (!DVC1124_ReadRegisterOnce(reg, &actual)) return 7u;
#else
                if (SH3673520_ReadRegOnce(reg, &actual) != SH3673520_OK) return 7u;
#endif
            } else if (!register_read(reg, &actual)) return 7u;
        } else {
            uint8_t result;
            if (!bms_afe_register_debug_held()) return 8u;
            mask = request[6];
            if (!mask || (mask & (uint8_t)~s_register_write_masks[reg - REGISTER_FIRST])) return 5u;
            result = register_update(reg, mask, request[7], request[8], 1u, &actual);
            if (result) return result;
        }
        payload[0] = reg; payload[1] = actual; *payload_length = 2u;
        return 0u;
    }
#endif
    return 3u;
}
#endif
#endif
