/*
 * 文件功能：公共业务参数只读能力、读写校验和分组恢复；遵守既有参数授权与持久化事务。
 * bms/core/bms_parameter_access.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_parameter_access.h"
#include "bms_product.h"
#include "bms_update_policy.h"
#include "bms_config_store.h"
#include "bms_state_store.h"
#include "bms_afe_hw_access.h"
#include "bms_features.h"
#include "bms_afe.h"
#include "bms_board.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include <string.h>

static u16 s_sequence, s_result, s_sn_generation, s_sn_mask;
static u32 s_sn_tick;
static char s_sn_stage[32];
static u8 s_sn_active;

/* 从大端字节缓冲区解码一个 16 位协议字。 */
static u16 word(const u8 *p)
{
    return ((u16)p[0] << 8) | p[1];
}
/* 按低字在前的协议字序解码 32 位值。 */
static u32 dword(const u8 *p)
{
    return (u32)word(p) | ((u32)word(p + 2) << 16);
}
/* 发布参数写入结果，成功时递增事务序号。 */
static u8 finish(u8 result)
{
    s_result = result;
    if (!result) ++s_sequence;
    return result;
}

/* 检查敏感出厂参数写入资格。 */
static u8 sensitive_factory_write_allowed(void)
{
    if (!bms_afe_hw_access_is_active()) return 0u;
    return 1u; /* 已授权的 AFE 独占会话，不再依赖老化时长。 */
}

/* 检查业务参数地址是否允许读取。 */
int bms_parameter_readable(u16 r)
{
    return (r >= BMS_PARAM_REG_MAGIC && r <= BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_STOP) || (r >= BMS_PARAM_REG_HEATER_ENABLE && r <= BMS_PARAM_REG_HEATER_STOP) ||
           (r >= BMS_PARAM_REG_CURRENT_OFFSET_LO && r <= BMS_PARAM_REG_SAMPLE_TICK_HI) || (r >= BMS_PARAM_REG_SN_BASE && r <= BMS_PARAM_REG_SN_LAST) ||
           (r >= BMS_PARAM_REG_BALANCE_ENABLE && r <= BMS_PARAM_REG_BALANCE_DELTA_STOP) ||
           (r >= BMS_PARAM_REG_UPDATE_REVISION_BASE && r < BMS_PARAM_REG_UPDATE_REVISION_BASE + BMS_UPDATE_GROUP_COUNT) || r == BMS_PARAM_REG_SOC || r == BMS_PARAM_REG_CAPACITY ||
           r == BMS_PARAM_REG_CYCLE;
}

/* 按地址读取业务参数并编码为协议字。 */
u16 bms_parameter_read(u16 r)
{
    bms_user_params_t v;
    bms_config_system_params_t system;
    bms_afe_aux_measurements_t sample;
    u8 sample_valid;
    if (r >= BMS_PARAM_REG_UPDATE_REVISION_BASE && r < BMS_PARAM_REG_UPDATE_REVISION_BASE + BMS_UPDATE_GROUP_COUNT)
        return bms_update_revision((bms_update_group_t)(r - BMS_PARAM_REG_UPDATE_REVISION_BASE));
    if (r == BMS_PARAM_REG_MAGIC) return BMS_PARAMETER_INTERFACE_MAGIC;
    if (r == BMS_PARAM_REG_VERSION) return 2u;
    if (r == BMS_PARAM_REG_CAPABILITIES) return 0x007Fu; /*
     * 容量、SN、加热、校准、重置、State 同步与均衡。
     */
    if (r == BMS_PARAM_REG_LAST_RESULT) return s_result;
    if (r == BMS_PARAM_REG_SEQUENCE) return s_sequence;
    if (r == BMS_PARAM_REG_CONFIG_FORMAT) return 2u; /* 配置格式：CFG2 schema。 */
    if (r == BMS_PARAM_REG_SN_GENERATION) return s_sn_generation;
    if (r == BMS_PARAM_REG_RESET_POLICY) return 1u; /* 禁止旧 1102=3 重置入口。 */
    if (r == BMS_PARAM_REG_DEFAULT_CAPACITY) return CapacityFactory;
    if (r == BMS_PARAM_REG_HEATER_SUPPORTED) return bms_board_heater_supported();
    if (r == BMS_PARAM_REG_DEFAULT_HEATER_START) return BMS_HEATER_START_TEMP_X10;
    if (r == BMS_PARAM_REG_DEFAULT_HEATER_STOP) return BMS_HEATER_STOP_TEMP_X10;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_ENABLE) return BMS_BALANCE_ENABLE_DEFAULT;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_START) return BMS_BALANCE_START_VOLTAGE_MV_DEFAULT;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_START) return BMS_BALANCE_START_DELTA_MV_DEFAULT;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_STOP) return BMS_BALANCE_STOP_DELTA_MV_DEFAULT;
    if (r == BMS_PARAM_REG_SOC) return get_soc_real();
    if (r == BMS_PARAM_REG_CYCLE) return (u16)g_bms_soc.u32Cycle_times;
    if (r == BMS_PARAM_REG_CAPACITY)
        return bms_config_store_get_system(&system) ? (u16)system.capacity_factory : 0xFFFFu;
    if (r >= BMS_PARAM_REG_RAW_CURRENT_LO && r <= BMS_PARAM_REG_SAMPLE_TICK_HI)
    {
        memset(&sample, 0, sizeof(sample));
        sample_valid = bms_afe_get_aux_measurements(&sample);
        if (r == BMS_PARAM_REG_SAMPLE_VALID)
            return sample_valid &&
                   (u32)(pm_get_32k_tick() - sample.sample_tick_32k) <= BMS_SOC_MAX_SAMPLE_GAP_32K;
        if (r == BMS_PARAM_REG_SAMPLE_TICK_LO) return (u16)sample.sample_tick_32k;
        if (r == BMS_PARAM_REG_SAMPLE_TICK_HI) return (u16)(sample.sample_tick_32k >> 16);
        if (r == BMS_PARAM_REG_RAW_CURRENT_LO) return (u16)sample.raw_current_ma;
        if (r == BMS_PARAM_REG_RAW_CURRENT_HI) return (u16)((u32)sample.raw_current_ma >> 16);
        if (r == BMS_PARAM_REG_CURRENT_LO) return (u16)sample.current_ma;
        return (u16)((u32)sample.current_ma >> 16);
    }
    if (!bms_config_get_user(&v)) return 0xFFFFu;
    if (r == BMS_PARAM_REG_HEATER_ENABLE) return v.heater_enable;
    if (r == BMS_PARAM_REG_HEATER_START) return v.heater_start_x10;
    if (r == BMS_PARAM_REG_HEATER_STOP) return v.heater_stop_x10;
    if (r == BMS_PARAM_REG_BALANCE_ENABLE) return v.balance_enable;
    if (r == BMS_PARAM_REG_BALANCE_START) return v.balance_start_mv;
    if (r == BMS_PARAM_REG_BALANCE_DELTA_START) return v.balance_start_delta_mv;
    if (r == BMS_PARAM_REG_BALANCE_DELTA_STOP) return v.balance_stop_delta_mv;
    if (r == BMS_PARAM_REG_CURRENT_OFFSET_LO) return (u16)v.current_offset_ma;
    if (r == BMS_PARAM_REG_CURRENT_OFFSET_HI) return (u16)((u32)v.current_offset_ma >> 16);
    if (r == BMS_PARAM_REG_CURRENT_GAIN_LO) return (u16)v.current_gain_ppm;
    if (r == BMS_PARAM_REG_CURRENT_GAIN_HI) return (u16)(v.current_gain_ppm >> 16);
    if (r >= BMS_PARAM_REG_SN_BASE && r <= BMS_PARAM_REG_SN_LAST)
    {
        u16 i = (u16)((r - BMS_PARAM_REG_SN_BASE) * 2u);
        return ((u16)(u8)v.serial[i] << 8) | (u8)v.serial[i + 1u];
    }
    return 0u;
}

/* 校验并写入业务参数，按类别执行持久化。 */
u8 bms_parameter_write(u16 r, u16 qty, const u8 *data)
{
    bms_user_params_t v;
    bms_config_system_params_t system;
    u16 value, i;
    if (!data || !qty) return finish(3u);
    value = word(data);
    if (r == BMS_PARAM_REG_SOC || r == BMS_PARAM_REG_CYCLE)
    {
        u16 soc = (r == BMS_PARAM_REG_SOC) ? value : get_soc_real();
        u32 cycle = (r == BMS_PARAM_REG_CYCLE) ? value : g_bms_soc.u32Cycle_times;
        if (qty != 1u || soc > 100u) return finish(3u);
        if (!bms_state_store_set_soc_cycle(soc, g_bms_soc.u8DSG_SOC_Int, cycle))
            return finish(4u);
        g_bms_soc.u32Cycle_times = cycle;
        set_soc_param((u8)soc, 1u);
        return finish(0u);
    }
    if (r == BMS_PARAM_REG_CAPACITY)
    {
        if (qty != 1u || !value || value > BMS_SOC_CAPACITY_MAX_0P1AH) return finish(3u);
        if (!bms_config_store_get_system(&system)) return finish(4u);
        if (system.capacity_factory == value) return finish(0u);
        system.capacity_factory = value;
        if (!bms_config_store_set_system(&system)) return finish(4u);
        bms_soc_nominal_capacity_changed();
        return finish(0u);
    }
    if (r == BMS_PARAM_REG_RESET_COMMAND)
    {
        if (qty != 1u) return finish(3u);
        if (value == 1u) return finish(bms_reset_software_parameters());
        if (value == 2u) return finish(bms_reset_afe_parameters());
        if (value == 3u)
        {
            if (!bms_config_reset_business()) return finish(4u);
            bms_soc_nominal_capacity_changed();
            return finish(0u);
        }
        return finish(3u);
    }
    if (!bms_config_get_user(&v)) return finish(4u);
    if (r == BMS_PARAM_REG_HEATER_ENABLE)
    {
        if (qty != 3u) return finish(3u);
        v.heater_enable = value;
        v.heater_start_x10 = word(data + 2);
        v.heater_stop_x10 = word(data + 4);
    }
    else if (r == BMS_PARAM_REG_BALANCE_ENABLE)
    {
        if (qty != 4u) return finish(3u);
        v.balance_enable = value;
        v.balance_start_mv = word(data + 2);
        v.balance_start_delta_mv = word(data + 4);
        v.balance_stop_delta_mv = word(data + 6);
    }
    else if (r == BMS_PARAM_REG_CURRENT_OFFSET_LO)
    {
        if (qty != 4u) return finish(3u);
        if (!sensitive_factory_write_allowed()) return finish(2u);
        v.current_offset_ma = (int32_t)dword(data);
        v.current_gain_ppm = dword(data + 4);
    }
    else if (r == BMS_PARAM_REG_SN_COMMAND)
    {
        if (qty != 1u || !sensitive_factory_write_allowed()) return finish(2u);
        if (value == 0u)
        {
            if (++s_sn_generation == 0u) ++s_sn_generation;
            s_sn_mask = 0u;
            s_sn_active = 1u;
            s_sn_tick = pm_get_32k_tick();
            memset(s_sn_stage, 0, sizeof(s_sn_stage));
            return finish(0u);
        }
        if (!s_sn_active || value != s_sn_generation || s_sn_mask != 0xFFFFu ||
            (u32)(pm_get_32k_tick() - s_sn_tick) > 60u * 32000u)
            return finish(3u);
        s_sn_active = 0u;
        memcpy(v.serial, s_sn_stage, sizeof(v.serial));
    }
    else if (r >= BMS_PARAM_REG_SN_STAGE_BASE && r <= BMS_PARAM_REG_SN_STAGE_LAST)
    {
        if (!sensitive_factory_write_allowed()) return finish(2u);
        if (!s_sn_active || (u32)(pm_get_32k_tick() - s_sn_tick) > 60u * 32000u || qty > 4u ||
            (u32)r + qty > BMS_PARAM_REG_SN_STAGE_END)
            return finish(3u);
        for (i = 0u; i < qty; ++i)
        {
            u16 slot = (u16)(r - BMS_PARAM_REG_SN_STAGE_BASE + i);
            s_sn_stage[slot * 2u] = (char)data[i * 2u];
            s_sn_stage[slot * 2u + 1u] = (char)data[i * 2u + 1u];
            s_sn_mask |= (u16)(1u << slot);
        }
        return finish(0u); /* 仅暂存，不写 Flash。 */
    }
    else return finish(2u);
    if (!bms_config_user_valid(&v)) return finish(3u);
    if (!bms_config_set_user(&v)) return finish(4u);
    if (r == BMS_PARAM_REG_SN_COMMAND) bms_product_info_refresh();
    return finish(0u);
}
