/*
 * 文件功能：软件三级保护滤波；边界采集 const 输入，私有 Third 负责软件 MOS 阻断。
 * bms/core/bms_sw_protection.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_sw_protection.h"

#include "bms_error.h"
#include "bms_state.h"
#include "bms_protection_params.h"
#include <string.h>

/*
 * D008 / D011 / D013 / D014 共享软件保护策略：仅管理软件阈值、滤波与恢复状态，
 * 不访问 AFE/GPIO，不清除硬件锁存。一级/二级用于报告和告警，三级用于软件 MOS 阻断；
 * AFE 硬件保护保持独立后备，后端在软件计算后合并硬件标志到 Third。
 * SOC 低电量故障由 bms_soc.c 根据 SOC 样本统一处理。
 */
#define BMS_SW_PROTECTION_SAMPLE_MS       200u
#define BMS_SW_PROTECTION_LEVEL_COUNT     3u
#define BMS_SW_PROTECTION_FILTER_COUNT    12u

typedef struct
{
    uint16_t trip_count;
    uint16_t recover_count;
    uint8_t active;
} bms_sw_filter_t;

typedef enum
{
    BMS_SW_F_CELL_OV = 0,
    BMS_SW_F_CELL_UV,
    BMS_SW_F_PACK_OV,
    BMS_SW_F_PACK_UV,
    BMS_SW_F_CHG_OC,
    BMS_SW_F_DSG_OC,
    BMS_SW_F_CHG_OT,
    BMS_SW_F_CHG_UT,
    BMS_SW_F_DSG_OT,
    BMS_SW_F_DSG_UT,
    BMS_SW_F_MOS_OT,
    BMS_SW_F_VDELTA,
    BMS_SW_F_COUNT
} bms_sw_filter_id_t;

typedef enum
{
    BMS_SW_HIGH = 0,
    BMS_SW_LOW
} bms_sw_direction_t;

/* 各等级/项目独立保存触发累积和连续恢复计数，单位为有效样本。 */
static bms_sw_filter_t s_filter[BMS_SW_PROTECTION_LEVEL_COUNT][BMS_SW_F_COUNT];
static bms_fault_reg_t s_prev_fault[BMS_SW_PROTECTION_LEVEL_COUNT];
/* 软件拥有这些位，公共报告还可包含硬件位。 */
static bms_fault_reg_t s_sw_fault[BMS_SW_PROTECTION_LEVEL_COUNT];
static uint8_t s_current_recovery_requires_evidence;

/* 恢复仅属于阻断 MOS 的 Third 级；First/Second 是滤波告警，各自越限消失即清除。 */
/* 校验高阈值三级保护的恢复值必须低于触发值。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static uint8_t bms_sw_high_recovery_valid(uint16_t third, uint16_t recover) __attribute__((noinline));
#endif
static uint8_t bms_sw_high_recovery_valid(uint16_t third, uint16_t recover)
{
    return !third || recover < third;
}

/* 校验低阈值保护的恢复值与触发值关系。 */
static uint8_t bms_sw_low_recovery_valid(uint16_t third, uint16_t recover)
{
    return !third || recover > third;
}

/* 按保护级别选择对应阈值或延时。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static uint16_t bms_sw_level_value(uint8_t level,
                                   uint16_t first,
                                   uint16_t second,
                                   uint16_t third) __attribute__((noinline));
#endif
static uint16_t bms_sw_level_value(uint8_t level,
                                   uint16_t first,
                                   uint16_t second,
                                   uint16_t third)
{
    return (level == 0u) ? first : ((level == 1u) ? second : third);
}

/* 把软件保护延时转换为连续样本数。 */
static uint16_t bms_sw_filter_samples(uint16_t filter_10ms)
{
    uint32_t delay_ms = (uint32_t)filter_10ms * 10u;
    uint32_t samples;

    if (delay_ms == 0u) return 1u;
    samples = (delay_ms + BMS_SW_PROTECTION_SAMPLE_MS - 1u) /
              BMS_SW_PROTECTION_SAMPLE_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

/* 清除软件保护滤波器的计数与活动状态。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static void bms_sw_filter_reset(bms_sw_filter_t *state) __attribute__((noinline));
#endif
static void bms_sw_filter_reset(bms_sw_filter_t *state)
{
    if (state == 0) return;
    state->trip_count = 0u;
    state->recover_count = 0u;
    state->active = 0u;
}

/* 触发按超限累积、正常样本递减；恢复要求连续样本。保留既有滤波行为。 */
static uint8_t bms_sw_filter_update(bms_sw_filter_t *state,
                                    uint16_t value,
                                    uint16_t trip,
                                    uint16_t recover,
                                    uint16_t filter_10ms,
                                    bms_sw_direction_t direction,
                                    uint8_t use_recovery)
{
    uint16_t required;
    uint8_t violated;
    uint8_t recovered;

    if (state == 0) return 0u;
    if (trip == 0u)
    {
        bms_sw_filter_reset(state);
        return 0u;
    }

    required = bms_sw_filter_samples(filter_10ms);
    if (state->active)
    {
        if (use_recovery)
            recovered = (direction == BMS_SW_HIGH) ?
                        (value <= recover) : (value >= recover);
        else
            recovered = (direction == BMS_SW_HIGH) ?
                        (value < trip) : (value > trip);
        if (recovered)
        {
            if (state->recover_count < required) ++state->recover_count;
            if (state->recover_count >= required)
                bms_sw_filter_reset(state);
        }
        else
        {
            state->recover_count = 0u;
        }
        return state->active;
    }

    violated = (direction == BMS_SW_HIGH) ?
               (value >= trip) : (value <= trip);
    if (violated)
    {
        if (state->trip_count < required) ++state->trip_count;
        if (state->trip_count >= required)
        {
            state->active = 1u;
            state->trip_count = 0u;
            state->recover_count = 0u;
        }
    }
    else if (state->trip_count != 0u)
    {
        /* 保留 D011/D013 既有漏积分去抖，不因一个正常样本就清越限累计。 */
        --state->trip_count;
    }
    return state->active;
}

/*
 * 电池温度故障按方向触发：充电高/低温仅在充电电流存在时建立，放电同理。
 * 方向门禁只用于新故障确认；动作后电流通常消失，
 * 若因此清故障会立即重新开 FET 并反复通断。因此活动故障仅按温度与恢复滤波解除，
 * 不再依赖电流。
 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static uint8_t bms_sw_temp_filter_update(bms_sw_filter_t *state,
                                         uint8_t trip_enabled,
                                         uint16_t value,
                                         uint16_t trip,
                                         uint16_t recover,
                                         uint16_t filter_10ms,
                                         bms_sw_direction_t direction,
                                         uint8_t use_recovery) __attribute__((noinline));
#endif
static uint8_t bms_sw_temp_filter_update(bms_sw_filter_t *state,
                                         uint8_t trip_enabled,
                                         uint16_t value,
                                         uint16_t trip,
                                         uint16_t recover,
                                         uint16_t filter_10ms,
                                         bms_sw_direction_t direction,
                                         uint8_t use_recovery)
{
    if (state == 0) return 0u;

    if (!state->active && !trip_enabled)
    {
        /*
         * 整个触发确认期间温度和对应电流方向须同时满足，
         * 不能跨空闲或反向保留部分计数。
         */
        state->trip_count = 0u;
        state->recover_count = 0u;
        return 0u;
    }

    return bms_sw_filter_update(state, value, trip, recover,
                                filter_10ms, direction, use_recovery);
}

/* 取得指定级别的软件故障寄存器。 */
static bms_fault_reg_t *bms_sw_fault_reg(uint8_t level)
{
    if (level == 0u) return &g_bms_report.fault_first;
    if (level == 1u) return &g_bms_report.fault_second;
    return &g_bms_report.fault_third;
}

/* 仅清除本模块拥有的软件保护故障位。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static void bms_sw_clear_managed_bits(bms_fault_reg_t *fault) __attribute__((noinline));
#endif
static void bms_sw_clear_managed_bits(bms_fault_reg_t *fault)
{
    if (fault == 0) return;
    fault->bits.cell_ovp = 0u;
    fault->bits.cell_uvp = 0u;
    fault->bits.pack_ovp = 0u;
    fault->bits.pack_uvp = 0u;
    fault->bits.charge_ocp = 0u;
    fault->bits.discharge_ocp = 0u;
    fault->bits.charge_otp = 0u;
    fault->bits.charge_utp = 0u;
    fault->bits.discharge_otp = 0u;
    fault->bits.discharge_utp = 0u;
    fault->bits.mos_otp = 0u;
    fault->bits.cell_delta_high = 0u;
}

/* 检查软件保护阈值、恢复值和延时关系。 */
uint8_t bms_sw_protection_validate_params(const bms_protection_params_t *p)
{
    if (p == 0) return 0u;
    if ((p->cell_ovp_first_mv > p->cell_ovp_second_mv) ||
        (p->cell_ovp_second_mv > p->cell_ovp_third_mv) ||
        (p->pack_ovp_first_10mv > p->pack_ovp_second_10mv) ||
        (p->pack_ovp_second_10mv > p->pack_ovp_third_10mv) ||
        (p->charge_ocp_first_a10 > p->charge_ocp_second_a10) ||
        (p->charge_ocp_second_a10 > p->charge_ocp_third_a10) ||
        (p->discharge_ocp_first_a10 > p->discharge_ocp_second_a10) ||
        (p->discharge_ocp_second_a10 > p->discharge_ocp_third_a10) ||
        (p->charge_otp_first_x10 > p->charge_otp_second_x10) ||
        (p->charge_otp_second_x10 > p->charge_otp_third_x10) ||
        (p->discharge_otp_first_x10 > p->discharge_otp_second_x10) ||
        (p->discharge_otp_second_x10 > p->discharge_otp_third_x10) ||
        (p->mos_otp_first_x10 > p->mos_otp_second_x10) ||
        (p->mos_otp_second_x10 > p->mos_otp_third_x10) ||
        (p->cell_delta_first_mv > p->cell_delta_second_mv) ||
        (p->cell_delta_second_mv > p->cell_delta_third_mv)) return 0u;
    if ((p->cell_uvp_first_mv < p->cell_uvp_second_mv) ||
        (p->cell_uvp_second_mv < p->cell_uvp_third_mv) ||
        (p->pack_uvp_first_10mv < p->pack_uvp_second_10mv) ||
        (p->pack_uvp_second_10mv < p->pack_uvp_third_10mv) ||
        (p->charge_utp_first_x10 < p->charge_utp_second_x10) ||
        (p->charge_utp_second_x10 < p->charge_utp_third_x10) ||
        (p->discharge_utp_first_x10 < p->discharge_utp_second_x10) ||
        (p->discharge_utp_second_x10 < p->discharge_utp_third_x10)) return 0u;
    if (!bms_sw_high_recovery_valid(p->cell_ovp_third_mv, p->cell_ovp_recover_mv) ||
        !bms_sw_high_recovery_valid(p->pack_ovp_third_10mv, p->pack_ovp_recover_10mv) ||
        !bms_sw_high_recovery_valid(p->charge_ocp_third_a10, p->charge_ocp_recover_a10) ||
        !bms_sw_high_recovery_valid(p->discharge_ocp_third_a10, p->discharge_ocp_recover_a10) ||
        !bms_sw_high_recovery_valid(p->charge_otp_third_x10, p->charge_otp_recover_x10) ||
        !bms_sw_high_recovery_valid(p->discharge_otp_third_x10, p->discharge_otp_recover_x10) ||
        !bms_sw_high_recovery_valid(p->mos_otp_third_x10, p->mos_otp_recover_x10) ||
        !bms_sw_high_recovery_valid(p->cell_delta_third_mv, p->cell_delta_recover_mv)) return 0u;
    if (!bms_sw_low_recovery_valid(p->cell_uvp_third_mv, p->cell_uvp_recover_mv) ||
        !bms_sw_low_recovery_valid(p->pack_uvp_third_10mv, p->pack_uvp_recover_10mv) ||
        !bms_sw_low_recovery_valid(p->charge_utp_third_x10, p->charge_utp_recover_x10) ||
        !bms_sw_low_recovery_valid(p->discharge_utp_third_x10, p->discharge_utp_recover_x10)) return 0u;
    if (p->charge_otp_third_x10 > 1450u || p->charge_otp_recover_x10 > 1450u ||
        p->charge_utp_third_x10 > 1450u || p->charge_utp_recover_x10 > 1450u ||
        p->discharge_otp_third_x10 > 1450u || p->discharge_otp_recover_x10 > 1450u ||
        p->discharge_utp_third_x10 > 1450u || p->discharge_utp_recover_x10 > 1450u ||
        p->mos_otp_third_x10 > 1450u || p->mos_otp_recover_x10 > 1450u) return 0u;
    return 1u;
}

/* 清除软件保护状态与内部滤波计数。 */
void bms_sw_protection_clear(void)
{
    uint8_t level;
    memset(s_filter, 0, sizeof(s_filter));
    memset(s_sw_fault, 0, sizeof(s_sw_fault));
    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
        bms_sw_clear_managed_bits(bms_sw_fault_reg(level));
    bms_error_clear(BMS_ERROR_TEMP_BREAK);
}

/* 初始化软件保护参数和状态。 */
void bms_sw_protection_init(void)
{
    uint8_t charge_oc = s_current_recovery_requires_evidence &&
                        s_filter[2][BMS_SW_F_CHG_OC].active;
    uint8_t discharge_oc = s_current_recovery_requires_evidence &&
                           s_filter[2][BMS_SW_F_DSG_OC].active;
    memset(s_prev_fault, 0, sizeof(s_prev_fault));
    bms_sw_protection_clear();
    /*
     * AFE 通信重初始化不能证明负载/充电器已移除；保留故障，只作废部分恢复窗口。
     * MCU 冷启动仍从零初始化 RAM 开始，此状态不持久化。
     */
    s_filter[2][BMS_SW_F_CHG_OC].active = charge_oc;
    s_filter[2][BMS_SW_F_DSG_OC].active = discharge_oc;
    s_sw_fault[2].bits.charge_ocp = charge_oc;
    s_sw_fault[2].bits.discharge_ocp = discharge_oc;
    bms_sw_fault_reg(2u)->all |= s_sw_fault[2].all;
}

/* 复位电流保护的恢复确认状态。 */
void bms_sw_protection_reset_current_recovery(void)
{
    s_filter[2][BMS_SW_F_CHG_OC].recover_count = 0u;
    s_filter[2][BMS_SW_F_DSG_OC].recover_count = 0u;
}

/* 查询软件放电过流三级保护是否激活。 */
uint8_t bms_sw_protection_discharge_overcurrent_active(void)
{
    return s_filter[2][BMS_SW_F_DSG_OC].active;
}

/* 按当前测量快照更新软件保护状态。 */
void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs)
{
    bms_sw_protection_update_groups(inputs, 1u, 1u);
}

typedef struct {
    uint16_t cell_max_mv, cell_min_mv, cell_delta_mv;
    uint16_t pack_voltage_10mv, charge_a10, discharge_a10;
} bms_sw_measurements_t;

/* 结合物理恢复证据推进电流保护触发与解除滤波。 */
static uint8_t bms_sw_current_filter_update(bms_sw_filter_t *state,
    uint16_t value, uint16_t trip, uint16_t recover, uint16_t filter_10ms,
    uint8_t third, uint8_t recovery_allowed, uint8_t fresh)
{
    if (third && state->active && s_current_recovery_requires_evidence && trip != 0u) {
        if (!recovery_allowed) state->recover_count = 0u;
        /* 正常 CADC 等待暂停计数；观测到重接则重置。 */
        if (!recovery_allowed || !fresh) return 1u;
    }
    return bms_sw_filter_update(state, value, trip, recover, filter_10ms,
                                BMS_SW_HIGH, third);
}

/* 以输入快照评估各组软件保护并更新故障状态。 */
static void bms_sw_evaluate(const bms_sw_protection_inputs_t *inputs,
                            const bms_protection_params_t *p,
                            const bms_sw_measurements_t *measurements,
                            uint8_t voltage_current_enabled,
                            uint8_t temperature_enabled)
{
    uint8_t level;
    uint8_t charge_current_present;
    uint8_t discharge_current_present;

    charge_current_present = (measurements->charge_a10 > 0u) ? 1u : 0u;
    discharge_current_present = (measurements->discharge_a10 > 0u) ? 1u : 0u;

    /*
     * 系统仍按温度断线保持故障安全，但每个温度保护分组只使用自身传感器。
     * MOS NTC 缺失不能清除电池高/低温状态，反之亦然。
     */
    if (!temperature_enabled || (inputs->battery_temp_valid &&
        (!inputs->mos_temp_required || inputs->mos_temp_valid)))
        bms_error_clear(BMS_ERROR_TEMP_BREAK);
    else
        bms_error_raise(BMS_ERROR_TEMP_BREAK);

    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
    {
        bms_fault_reg_t *f = &s_sw_fault[level];
        uint16_t trip;

        if (voltage_current_enabled) {
            if (!inputs->voltage_sample_diagnostic) {
                trip = bms_sw_level_value(level, p->cell_ovp_first_mv,
                                          p->cell_ovp_second_mv, p->cell_ovp_third_mv);
                f->bits.cell_ovp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CELL_OV],
                    measurements->cell_max_mv, trip, p->cell_ovp_recover_mv,
                    p->cell_ovp_filter_10ms, BMS_SW_HIGH, level == 2u);

                trip = bms_sw_level_value(level, p->cell_uvp_first_mv,
                                          p->cell_uvp_second_mv, p->cell_uvp_third_mv);
                f->bits.cell_uvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CELL_UV],
                    measurements->cell_min_mv, trip, p->cell_uvp_recover_mv,
                    p->cell_uvp_filter_10ms, BMS_SW_LOW, level == 2u);

                trip = bms_sw_level_value(level, p->pack_ovp_first_10mv,
                                          p->pack_ovp_second_10mv, p->pack_ovp_third_10mv);
                f->bits.pack_ovp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_PACK_OV],
                    measurements->pack_voltage_10mv, trip, p->pack_ovp_recover_10mv,
                    p->pack_ovp_filter_10ms, BMS_SW_HIGH, level == 2u);

                trip = bms_sw_level_value(level, p->pack_uvp_first_10mv,
                                          p->pack_uvp_second_10mv, p->pack_uvp_third_10mv);
                f->bits.pack_uvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_PACK_UV],
                    measurements->pack_voltage_10mv, trip, p->pack_uvp_recover_10mv,
                    p->pack_uvp_filter_10ms, BMS_SW_LOW, level == 2u);

            }
            trip = bms_sw_level_value(level, p->charge_ocp_first_a10,
                                      p->charge_ocp_second_a10, p->charge_ocp_third_a10);
            f->bits.charge_ocp = bms_sw_current_filter_update(&s_filter[level][BMS_SW_F_CHG_OC],
                measurements->charge_a10, trip, p->charge_ocp_recover_a10,
                p->charge_ocp_filter_10ms, level == 2u, inputs->charge_recovery_allowed,
                inputs->current_recovery_sample_fresh);

            trip = bms_sw_level_value(level, p->discharge_ocp_first_a10,
                                      p->discharge_ocp_second_a10, p->discharge_ocp_third_a10);
            f->bits.discharge_ocp = bms_sw_current_filter_update(&s_filter[level][BMS_SW_F_DSG_OC],
                measurements->discharge_a10, trip, p->discharge_ocp_recover_a10,
                p->discharge_ocp_filter_10ms, level == 2u, inputs->discharge_recovery_allowed,
                inputs->current_recovery_sample_fresh);

        } else {
            uint8_t id;
            for (id = BMS_SW_F_CELL_OV; id <= BMS_SW_F_DSG_OC; ++id)
                bms_sw_filter_reset(&s_filter[level][id]);
            f->bits.cell_ovp = f->bits.cell_uvp = 0u;
            f->bits.pack_ovp = f->bits.pack_uvp = 0u;
            f->bits.charge_ocp = f->bits.discharge_ocp = 0u;
        }

        if (temperature_enabled && inputs->battery_temp_valid)
        {
            trip = bms_sw_level_value(level, p->charge_otp_first_x10,
                                      p->charge_otp_second_x10, p->charge_otp_third_x10);
            f->bits.charge_otp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_CHG_OT],
                charge_current_present, inputs->battery_temp_max, trip, p->charge_otp_recover_x10,
                p->charge_otp_filter_10ms, BMS_SW_HIGH, level == 2u);

            trip = bms_sw_level_value(level, p->charge_utp_first_x10,
                                      p->charge_utp_second_x10, p->charge_utp_third_x10);
            f->bits.charge_utp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_CHG_UT],
                charge_current_present, inputs->battery_temp_min, trip, p->charge_utp_recover_x10,
                p->charge_utp_filter_10ms, BMS_SW_LOW, level == 2u);

            trip = bms_sw_level_value(level, p->discharge_otp_first_x10,
                                      p->discharge_otp_second_x10, p->discharge_otp_third_x10);
            f->bits.discharge_otp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_DSG_OT],
                discharge_current_present, inputs->battery_temp_max, trip, p->discharge_otp_recover_x10,
                p->discharge_otp_filter_10ms, BMS_SW_HIGH, level == 2u);

            trip = bms_sw_level_value(level, p->discharge_utp_first_x10,
                                      p->discharge_utp_second_x10, p->discharge_utp_third_x10);
            f->bits.discharge_utp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_DSG_UT],
                discharge_current_present, inputs->battery_temp_min, trip, p->discharge_utp_recover_x10,
                p->discharge_utp_filter_10ms, BMS_SW_LOW, level == 2u);
        }
        else
        {
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_OT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_UT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_DSG_OT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_DSG_UT]);
            f->bits.charge_otp = 0u;
            f->bits.charge_utp = 0u;
            f->bits.discharge_otp = 0u;
            f->bits.discharge_utp = 0u;
        }

        if (temperature_enabled && inputs->mos_temp_required && inputs->mos_temp_valid)
        {
            trip = bms_sw_level_value(level, p->mos_otp_first_x10,
                                      p->mos_otp_second_x10, p->mos_otp_third_x10);
            f->bits.mos_otp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_MOS_OT],
                inputs->mos_temp, trip, p->mos_otp_recover_x10,
                p->mos_otp_filter_10ms, BMS_SW_HIGH, level == 2u);
        }
        else
        {
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_MOS_OT]);
            f->bits.mos_otp = 0u;
        }

        if (voltage_current_enabled && !inputs->voltage_sample_diagnostic) {
            trip = bms_sw_level_value(level, p->cell_delta_first_mv,
                                      p->cell_delta_second_mv, p->cell_delta_third_mv);
            f->bits.cell_delta_high = bms_sw_filter_update(&s_filter[level][BMS_SW_F_VDELTA],
                measurements->cell_delta_mv, trip, p->cell_delta_recover_mv,
                p->cell_delta_filter_10ms, BMS_SW_HIGH, level == 2u);
        } else if (!voltage_current_enabled) {
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_VDELTA]);
            f->bits.cell_delta_high = 0u;
        }
        /* 兼容报告保留 SOC/其它位，后端随后合并硬件位。 */
        bms_sw_clear_managed_bits(bms_sw_fault_reg(level));
        bms_sw_fault_reg(level)->all |= f->all;
    }
}

/* 主循环边界取一次测量视图，再评估只读输入；参数只由主循环候选提交所有者发布。 */
void bms_sw_protection_update_groups(const bms_sw_protection_inputs_t *inputs,
                                     uint8_t voltage_current_enabled,
                                     uint8_t temperature_enabled)
{
    bms_sw_measurements_t measurements;
    if (inputs == 0) return;
    s_current_recovery_requires_evidence = inputs->current_recovery_requires_evidence;
    if (!bms_protection_params_valid())
    {
        bms_sw_protection_clear();
        return;
    }
    measurements.cell_max_mv = g_bms_report.cell_max_mv;
    measurements.cell_min_mv = g_bms_report.cell_min_mv;
    measurements.cell_delta_mv = g_bms_report.cell_delta_mv;
    measurements.pack_voltage_10mv = g_bms_report.pack_voltage_10mv;
    measurements.charge_a10 = g_bms_report.charge_current_a10;
    measurements.discharge_a10 = g_bms_report.discharge_current_a10;
    bms_sw_evaluate(inputs, &g_bms_protection_params, &measurements,
                    voltage_current_enabled, temperature_enabled);
}

/* 持久故障历史仅记录上升沿；运行日志同时记录发生和恢复位图，避免改变原历史语义。 */
void bms_sw_protection_record_fault_edges(void)
{
    uint8_t level;
    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
    {
        bms_fault_reg_t *now = bms_sw_fault_reg(level);
        bms_fault_reg_t *prev = &s_prev_fault[level];
        uint8_t base = (uint8_t)(1u + 13u * level);
#define BMS_SW_RISE(field, offset) \
        do { if (now->bits.field && !prev->bits.field) \
            bms_fault_history_record((bms_fault_code_t)(base + (offset))); } while (0)
        BMS_SW_RISE(cell_ovp, 0u);
        BMS_SW_RISE(cell_uvp, 1u);
        BMS_SW_RISE(pack_ovp, 2u);
        BMS_SW_RISE(pack_uvp, 3u);
        BMS_SW_RISE(charge_ocp, 4u);
        BMS_SW_RISE(discharge_ocp, 5u);
        BMS_SW_RISE(charge_otp, 6u);
        BMS_SW_RISE(charge_utp, 7u);
        BMS_SW_RISE(discharge_otp, 8u);
        BMS_SW_RISE(discharge_utp, 9u);
        BMS_SW_RISE(mos_otp, 10u);
        BMS_SW_RISE(cell_delta_high, 11u);
#undef BMS_SW_RISE
        if (prev->all != now->all)
            BMS_LOG(BMS_LOG_INFO, BMS_LOG_PROTECT, BMS_LOG_PROTECTION_EDGE,
                    level + 1u, ((uint32_t)prev->all << 16) | now->all);
        *prev = *now;
    }
}

/* 查询三级软件保护是否禁止充电。 */
uint8_t bms_sw_protection_charge_blocked(void)
{
    const bms_fault_bits_t *f = &s_sw_fault[2].bits;
    return (f->cell_ovp || f->pack_ovp || f->charge_ocp ||
            f->charge_otp || f->charge_utp || f->mos_otp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

/* 查询三级软件保护是否禁止放电。 */
uint8_t bms_sw_protection_discharge_blocked(void)
{
    const bms_fault_bits_t *f = &s_sw_fault[2].bits;
    return (f->cell_uvp || f->pack_uvp || f->discharge_ocp ||
            f->discharge_otp || f->discharge_utp || f->mos_otp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}
