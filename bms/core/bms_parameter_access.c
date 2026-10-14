/*
 * 文件功能：公共业务参数只读能力、读写校验和分组恢复；遵守既有参数授权与持久化事务。
 * bms/core/bms_parameter_access.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_parameter_access.h"
#include "bms_product.h"
#include "bms_diag.h"
#include "bms_update_policy.h"
#include "bms_config_store.h"
#include "bms_sleep_config.h"
#include "bms_soc_profile_store.h"
#include "bms_state_store.h"
#include "bms_afe_hw_access.h"
#include "bms_features.h"
#include "bms_afe.h"
#include "bms_board.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include <string.h>

static u16 s_sequence, s_result, s_sn_generation, s_sn_mask;
typedef enum {
    CHEMISTRY_SWITCH_IDLE = 0,
    CHEMISTRY_SWITCH_COMMITTED = 1,
    CHEMISTRY_SWITCH_APPLIED = 2,
    CHEMISTRY_SWITCH_FAILED = 3
} chemistry_switch_state_t;
static chemistry_switch_state_t s_chemistry_status;
static u32 s_sn_tick;
static char s_sn_stage[32];
static u8 s_sn_active;
static bms_soc_profile_config_t s_soc_profile_stage;
static u16 s_soc_profile_generation;
static u8 s_soc_profile_stage_status;
static u32 s_soc_profile_stage_tick;

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

/* 检查业务参数地址是否允许读取。 */
int bms_parameter_readable(u16 r)
{
    /* 旧地址保持；新增休眠组只开放两个明确的五 word 块。 */
    return ((r >= BMS_PARAM_REG_MAGIC &&
             r <= BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_STOP) ||
            (r >= BMS_PARAM_REG_HEATER_ENABLE &&
             r <= BMS_PARAM_REG_HEATER_STOP) ||
            (r >= BMS_PARAM_REG_CURRENT_OFFSET_LO &&
             r <= BMS_PARAM_REG_SAMPLE_TICK_HI) ||
            (r >= BMS_PARAM_REG_SN_BASE &&
             r <= BMS_PARAM_REG_SN_LAST) ||
            (r >= BMS_PARAM_REG_BALANCE_ENABLE &&
             r <= BMS_PARAM_REG_BALANCE_DELTA_STOP) ||
            (r >= BMS_PARAM_REG_SLEEP_BASE && r <= BMS_PARAM_REG_SLEEP_LAST) ||
            (r >= BMS_PARAM_REG_SLEEP_DEFAULT_BASE && r <= BMS_PARAM_REG_SLEEP_DEFAULT_LAST) ||
            (r >= BMS_PARAM_REG_UPDATE_REVISION_BASE &&
             r < BMS_PARAM_REG_UPDATE_REVISION_BASE + BMS_UPDATE_GROUP_COUNT) ||
            (r >= BMS_PARAM_REG_CHEMISTRY_BASE && r <= BMS_PARAM_REG_CHEMISTRY_LAST) ||
            (r >= BMS_PARAM_REG_SOC_PROFILE_BASE && r <= BMS_PARAM_REG_SOC_PROFILE_STAGE_STATUS) ||
            r == BMS_PARAM_REG_SOC ||
            r == BMS_PARAM_REG_CAPACITY ||
            r == BMS_PARAM_REG_CYCLE);
}

/* 按地址读取业务参数并编码为协议字。 */
u16 bms_parameter_read(u16 r)
{
    bms_user_params_t v;
    bms_config_system_params_t system;
    bms_afe_aux_measurements_t sample;
    bms_sleep_config_t sleep;
    u16 sleep_word;
    u8 sample_valid;
    if (r >= BMS_PARAM_REG_UPDATE_REVISION_BASE && r < BMS_PARAM_REG_UPDATE_REVISION_BASE + BMS_UPDATE_GROUP_COUNT)
        return bms_update_revision((bms_update_group_t)(r - BMS_PARAM_REG_UPDATE_REVISION_BASE));
    if (r == BMS_PARAM_REG_MAGIC) return BMS_PARAMETER_INTERFACE_MAGIC;
    if (r == BMS_PARAM_REG_VERSION) return 2u;
    if (r == BMS_PARAM_REG_CAPABILITIES) return 0x00FFu | BMS_PARAM_CAP_CHEMISTRY_SWITCH |
        BMS_PARAM_CAP_SOC_PROFILE; /*
     * 容量、SN、加热、校准、重置、State 同步、均衡及两档休眠。
     */
    if (r >= BMS_PARAM_REG_CHEMISTRY_BASE && r <= BMS_PARAM_REG_CHEMISTRY_LAST) {
        if (r == BMS_PARAM_REG_CHEMISTRY_BASE) return BMS_CHEMISTRY_SWITCH_MAGIC;
        if (r == BMS_PARAM_REG_CHEMISTRY_BASE + 1u) return BMS_CHEMISTRY_SWITCH_VERSION;
        if (r == BMS_PARAM_REG_CHEMISTRY_BASE + 4u) return s_chemistry_status;
        if (r >= BMS_PARAM_REG_CHEMISTRY_BASE + 5u) {
            bms_soc_profile_config_t profile;
            bms_soc_profile_store_get(&profile);
            if (r == BMS_PARAM_REG_CHEMISTRY_BASE + 5u) return profile.current_deadband_ma;
            if (r == BMS_PARAM_REG_CHEMISTRY_BASE + 6u) return profile.ocv_rest_prepare_s;
            return profile.ocv_error_band_percent;
        }
        if (!bms_config_store_get_system(&system)) return 0xFFFFu;
        return r == BMS_PARAM_REG_CHEMISTRY_BASE + 2u ?
            (u16)system.battery_chemistry : (u16)system.soc_profile_id;
    }
    if (r >= BMS_PARAM_REG_SOC_PROFILE_BASE && r <= BMS_PARAM_REG_SOC_PROFILE_STAGE_STATUS) {
        bms_soc_profile_config_t profile;
        if (r == BMS_PARAM_REG_SOC_PROFILE_BASE) return BMS_SOC_PROFILE_INTERFACE_MAGIC;
        if (r == BMS_PARAM_REG_SOC_PROFILE_BASE + 1u) return BMS_SOC_PROFILE_INTERFACE_VERSION;
        if (r == BMS_PARAM_REG_SOC_PROFILE_PERSISTED) return bms_soc_profile_store_is_persisted();
        if (r == BMS_PARAM_REG_SOC_PROFILE_GENERATION) return s_soc_profile_generation;
        if (r == BMS_PARAM_REG_SOC_PROFILE_STAGE_STATUS) return s_soc_profile_stage_status;
        bms_soc_profile_store_get(&profile);
        return bms_soc_profile_store_word(&profile,
            (u16)(r - BMS_PARAM_REG_SOC_PROFILE_BASE - 2u));
    }
    if (r == BMS_PARAM_REG_LAST_RESULT) return s_result;
    if (r == BMS_PARAM_REG_SEQUENCE) return s_sequence;
    if (r == BMS_PARAM_REG_CONFIG_FORMAT) return 2u; /* 配置格式：CFG2 schema。 */
    if (r == BMS_PARAM_REG_SN_GENERATION) return s_sn_generation;
    if (r == BMS_PARAM_REG_RESET_POLICY) return 1u; /* 禁止旧 1102=3 重置入口。 */
    if (r == BMS_PARAM_REG_DEFAULT_CAPACITY) return BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH;
    if (r == BMS_PARAM_REG_HEATER_SUPPORTED) return bms_board_heater_supported();
    if (r == BMS_PARAM_REG_DEFAULT_HEATER_START) return BMS_HEATER_START_TEMP_X10;
    if (r == BMS_PARAM_REG_DEFAULT_HEATER_STOP) return BMS_HEATER_STOP_TEMP_X10;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_ENABLE) return BMS_BALANCE_ENABLE_DEFAULT;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_START) return BMS_BALANCE_START_VOLTAGE_MV_DEFAULT;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_START) return BMS_BALANCE_START_DELTA_MV_DEFAULT;
    if (r == BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_STOP) return BMS_BALANCE_STOP_DELTA_MV_DEFAULT;
    if ((r >= BMS_PARAM_REG_SLEEP_BASE && r <= BMS_PARAM_REG_SLEEP_LAST) ||
        (r >= BMS_PARAM_REG_SLEEP_DEFAULT_BASE && r <= BMS_PARAM_REG_SLEEP_DEFAULT_LAST)) {
        u16 offset;
        if (r >= BMS_PARAM_REG_SLEEP_DEFAULT_BASE) {
            bms_sleep_config_defaults(&sleep);
            offset = r - BMS_PARAM_REG_SLEEP_DEFAULT_BASE;
        } else {
            bms_sleep_config_get(&sleep);
            offset = r - BMS_PARAM_REG_SLEEP_BASE;
        }
        memcpy(&sleep_word, (const u8 *)&sleep + offset * 2u, sizeof(sleep_word));
        return sleep_word;
    }
    if (r == BMS_PARAM_REG_SOC) return get_soc_real();
    if (r == BMS_PARAM_REG_CYCLE) return (u16)g_bms_soc.cycle_count;
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

/* 完整短帧校验密码，要求新鲜静置电流；不会把关 MOS 后零电流当作恢复证据。 */
static u8 switch_chemistry(const u8 *data, u16 qty)
{
    bms_afe_aux_measurements_t sample;
    bms_soc_profile_config_t active_profile;
    bms_soc_profile_config_t previous_profile;
    u16 chemistry = word(data);
    if (qty != 4u) return 3u;
    /* 包含结尾 NUL，拒绝截断、前缀和额外密码字节；不保存或记录密码。 */
    if (memcmp(data + 2u, BMS_SERVICE_DEFAULT_PASSWORD, BMS_SERVICE_PASSWORD_BYTES) != 0) return 2u;
    if (chemistry != BMS_SOC_CHEMISTRY_LFP && chemistry != BMS_SOC_CHEMISTRY_NMC) return 3u;
#if BMS_PRODUCT_ID == 8u
    if (chemistry == BMS_SOC_CHEMISTRY_NMC && BMS_PRODUCT_CELL_COUNT > 23u) return 3u;
#endif
    bms_soc_profile_store_get(&active_profile);
    if (!bms_protection_params_valid() || !bms_afe_bus_access_allowed() ||
        !(bms_afe_get_guard_diagnostic_bits() & DIAG_GUARD_SAMPLES_QUALIFIED) ||
        !bms_afe_get_aux_measurements(&sample) ||
        (u32)(pm_get_32k_tick() - sample.sample_tick_32k) > BMS_SOC_MAX_SAMPLE_GAP_32K ||
        !bms_soc_profile_store_valid(&active_profile) ||
        sample.current_ma > (int32_t)active_profile.current_deadband_ma ||
        sample.current_ma < -(int32_t)active_profile.current_deadband_ma) return 4u;
    s_chemistry_status = CHEMISTRY_SWITCH_FAILED;
    bms_afe_invalidate_configuration();
    bms_soc_profile_store_get(&previous_profile);
    if (!bms_soc_profile_store_reset((u8)chemistry)) return 4u;
    if (!bms_config_store_switch_chemistry((u8)chemistry)) {
        (void)bms_soc_profile_store_set(&previous_profile);
        return 4u;
    }
    s_chemistry_status = CHEMISTRY_SWITCH_COMMITTED;
    if (!bms_parameters_reload_configuration() || !bms_afe_hw_profile_apply_committed()) {
        s_chemistry_status = CHEMISTRY_SWITCH_FAILED;
        bms_afe_invalidate_configuration();
        return 4u;
    }
    s_chemistry_status = CHEMISTRY_SWITCH_APPLIED;
    return 0u;
}

/* SOC profile 命令使用 op、generation、完整 hs456\0，共 5 word/19-byte RTU 帧。 */
static u8 soc_profile_command(const u8 *data, u16 qty)
{
    bms_soc_config_t identity;
    u16 op, generation;
    if (qty != 5u) return 3u;
    if (memcmp(data + 4u, BMS_SERVICE_DEFAULT_PASSWORD,
               BMS_SERVICE_PASSWORD_BYTES) != 0) return 2u;
    op = word(data);
    generation = word(data + 2u);
    if (op == 1u) {
        bms_soc_profile_store_get(&s_soc_profile_stage);
        if (++s_soc_profile_generation == 0u) ++s_soc_profile_generation;
        s_soc_profile_stage_tick = pm_get_32k_tick();
        s_soc_profile_stage_status = 1u;
        return 0u;
    }
    if (op == 2u) {
        if (s_soc_profile_stage_status != 1u || generation != s_soc_profile_generation ||
            (u32)(pm_get_32k_tick() - s_soc_profile_stage_tick) > 60u * 32000u ||
            !bms_soc_profile_store_valid(&s_soc_profile_stage)) return 3u;
        s_soc_profile_stage_status = 3u;
        if (!bms_soc_profile_store_set(&s_soc_profile_stage) || !bms_soc_profile_reload()) return 4u;
        s_soc_profile_stage_status = 2u;
        return 0u;
    }
    if (op == 3u) {
        if (!bms_config_store_get_soc(&identity) ||
            !bms_soc_profile_store_reset(identity.chemistry) || !bms_soc_profile_reload()) return 4u;
        s_soc_profile_stage_status = 0u;
        return 0u;
    }
    return 3u;
}

/* 校验并写入业务参数，按类别执行持久化。 */
u8 bms_parameter_write(u16 r, u16 qty, const u8 *data)
{
    bms_user_params_t v;
    bms_config_system_params_t system;
    bms_sleep_config_t sleep;
    u16 value, i;
    if (!data || !qty) return finish(3u);
    value = word(data);
    if (r == BMS_PARAM_REG_CHEMISTRY_COMMAND) return finish(switch_chemistry(data, qty));
    if (r == BMS_PARAM_REG_SOC_PROFILE_COMMAND) return finish(soc_profile_command(data, qty));
    if (r >= BMS_PARAM_REG_SOC_PROFILE_STAGE_BASE && r <= BMS_PARAM_REG_SOC_PROFILE_STAGE_LAST) {
        u16 offset = (u16)(r - BMS_PARAM_REG_SOC_PROFILE_STAGE_BASE);
        if (s_soc_profile_stage_status != 1u || qty > 4u ||
            (u32)offset + qty > BMS_SOC_PROFILE_WORDS ||
            (u32)(pm_get_32k_tick() - s_soc_profile_stage_tick) > 60u * 32000u)
            return finish(3u);
        for (i = 0u; i < qty; ++i)
            (void)bms_soc_profile_store_set_word(&s_soc_profile_stage, (u16)(offset + i),
                                                 word(data + i * 2u));
        s_soc_profile_stage_tick = pm_get_32k_tick();
        return finish(0u);
    }
    if (r == BMS_PARAM_REG_SLEEP_BASE) {
        if (qty != BMS_SLEEP_CONFIG_WORDS) return finish(3u);
        for (i = 0u; i < BMS_SLEEP_CONFIG_WORDS; ++i) {
            u16 item = word(data + i * 2u);
            memcpy((u8 *)&sleep + i * 2u, &item, sizeof(item));
        }
        if (!bms_sleep_config_valid(&sleep)) return finish(3u);
        return finish(bms_sleep_config_set(&sleep) ? 0u : 4u);
    }
    if (r == BMS_PARAM_REG_SOC || r == BMS_PARAM_REG_CYCLE)
    {
        u16 soc = (r == BMS_PARAM_REG_SOC) ? value : get_soc_real();
        u32 cycle = (r == BMS_PARAM_REG_CYCLE) ? value : g_bms_soc.cycle_count;
        if (qty != 1u || soc > 100u) return finish(3u);
        if (!bms_state_store_set_soc_cycle(soc, g_bms_soc.discharge_fraction_percent, cycle))
            return finish(4u);
        g_bms_soc.cycle_count = cycle;
        set_soc_param((u8)soc);
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
        if (!bms_afe_hw_access_is_active()) return finish(2u);
        v.current_offset_ma = (int32_t)dword(data);
        v.current_gain_ppm = dword(data + 4);
    }
    else if (r == BMS_PARAM_REG_SN_COMMAND)
    {
        if (qty != 1u || !bms_afe_hw_access_is_active()) return finish(2u);
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
        if (!bms_afe_hw_access_is_active()) return finish(2u);
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
