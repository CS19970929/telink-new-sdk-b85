/*
 * 文件功能：公共均衡与 heater 策略；依据采样可信度、温度和保护状态仲裁，
 * 硬件动作交给 backend。
 * bms/app/bms_features.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_features.h"
#include "bms_config_store.h"

#include "bms_board.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_parameters.h"

#define BMS_BALANCE_TRUST_CONFIRM_SAMPLES \
    ((BMS_BALANCE_TRUST_CONFIRM_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)

#if BMS_FEATURE_HEATER_SUPPORTED
typedef enum {
    BMS_HEATER_IDLE = 0u,
    BMS_HEATER_ARMING = 1u,
    BMS_HEATER_ACTIVE = 2u
} bms_heater_state_t;
#endif

#if BMS_FEATURE_HEATER_SUPPORTED || BMS_FEATURE_BALANCE_SUPPORTED
typedef struct {
#if BMS_FEATURE_HEATER_SUPPORTED
    uint8_t heater_fuse_fired;
    bms_heater_state_t heater_state;
    uint16_t heater_off_hot_samples;
#endif

#if BMS_FEATURE_BALANCE_SUPPORTED
    uint8_t balance_temperature_paused; /* 仅温度回落确认可解除，AFE 重初始化不清除。 */
    uint8_t balance_off_pending; /* 必须确认关闭后才能重新选择通道。 */
    uint16_t balance_trust_samples;
    uint8_t balance_prev_cell_count;
    uint16_t balance_prev_cell_mv[BMS_AFE_FEATURE_MAX_CELLS];
#if BMS_DEBUG_LOG_ENABLE
    uint32_t balance_requested_mask;
#endif
#endif
} bms_feature_state_t;

/*
 * 公共功能的资格/阶段状态；仅由 service/init/AFE 失效处理入口更新，
 * 产品能力由配置提供。
 */
static bms_feature_state_t s_feature;
#endif

/* 对外状态的均衡掩码只在此处解码；回读结果与应用请求仍分别保存。 */
#if BMS_FEATURE_BALANCE_SUPPORTED
static uint32_t reported_balance_mask(void)
{
    return ((uint32_t)g_bms_report.balance_bits_high << 16) |
           g_bms_report.balance_bits_low;
}
#endif

/* 更新加热请求并驱动支持的板级输出。 */
#if BMS_FEATURE_HEATER_SUPPORTED
static void set_heater(uint8_t on)
{
    on = (on && bms_board_heater_supported()) ? 1u : 0u;
    bms_board_heater_set(on);
    g_bms_system_status.bits.heater_status = on;
}

/* 停止加热并复位当前加热阶段。 */
static void heater_idle(void)
{
    set_heater(0u);
    s_feature.heater_state = BMS_HEATER_IDLE;
}

/* 把加热确认延时转换为连续样本数。 */
static uint16_t heater_confirm_samples(void)
{
    uint32_t confirm_ms = bms_board_heater_off_fault_confirm_ms();
    uint32_t samples;

    if (confirm_ms == 0u) return 1u;
    samples = (confirm_ms + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) /
              BMS_FEATURE_SERVICE_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

/* 满足独立故障条件时触发支持的不可逆熔断输出。 */
static void fire_heater_fuse(void)
{
    heater_idle();
    if (!s_feature.heater_fuse_fired)
    {
        bms_board_heater_fuse_fire();
        s_feature.heater_fuse_fired = 1u;
    }
    if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
}

/*
 * 加热电路安全独立于功率 MOS 高温保护。GP1 测加热 MOS 区域；
 * 软件关闭 PA1/MCC-EN-HT 后，GP1 持续超温达到板级确认时间，
 * 认为加热功率路径卡住/故障，驱动 PD4/MCC-EN-RF 触发不可逆保险丝。
 * 加热期间过热先关闭命令，只有关闭后持续过热才推进不可逆动作。
 */
static uint8_t heater_circuit_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t trip;
    uint16_t required;

    if (s_feature.heater_fuse_fired)
    {
        heater_idle();
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    if (!s->heater_temp_valid)
    {
        s_feature.heater_off_hot_samples = 0u;
        heater_idle();
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    trip = bms_board_heater_off_fault_temp_x10();
    if (!bms_board_heater_fuse_supported() || (trip == 0u))
    {
        s_feature.heater_off_hot_samples = 0u;
        bms_error_clear(BMS_ERROR_HEAT);
        return 1u;
    }

    if (s->heater_temp_x10 < trip)
    {
        s_feature.heater_off_hot_samples = 0u;
        bms_error_clear(BMS_ERROR_HEAT);
        return 1u;
    }

    if (g_bms_system_status.bits.heater_status)
    {
        s_feature.heater_off_hot_samples = 0u;
        set_heater(0u);
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    required = heater_confirm_samples();
    if (s_feature.heater_off_hot_samples < required)
        ++s_feature.heater_off_hot_samples;
    if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
    set_heater(0u);

    if (s_feature.heater_off_hot_samples >= required)
        fire_heater_fuse();
    return 0u;
}

/* 检查必须立即停止加热的硬故障条件。 */
static uint8_t heater_hard_fault(void)
{
    const bms_fault_bits_t *f = &g_bms_report.fault_third.bits;

    /*
     * 充放电低温不作为加热硬故障，低温正是预热要恢复的条件；CUV 也不是加热硬故障，
     * 深度放电包可能需预热后才能安全充电。
     */
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            f->cell_ovp || f->pack_ovp ||
            f->charge_ocp || f->discharge_ocp ||
            f->charge_otp || f->discharge_otp ||
            f->mos_otp) ? 1u : 0u;
}

/* 判断电池温度是否形成加热需求。 */
static uint8_t heater_demand(const bms_afe_feature_snapshot_t *s,
                             const bms_user_params_t *config)
{
    uint16_t charge_utp_trip;


    /*
     * 不能等待滤波后的充电低温故障位再预热；首个可信电流样本就需停止低温充电尝试。
     * HeaterStart 是用户策略，Third UTP 阈值仍是绝对软件充电许可边界。
     */
    charge_utp_trip = g_bms_protection_params.charge_utp_third_x10;
    if ((charge_utp_trip != 0u) &&
        (s->battery_temp_min_x10 <= charge_utp_trip))
        return 1u;
    if (g_bms_report.fault_third.bits.charge_utp) return 1u;

    if (s_feature.heater_state == BMS_HEATER_ACTIVE)
        return (s->battery_temp_min_x10 < config->heater_stop_x10) ? 1u : 0u;
    return (s->battery_temp_min_x10 < config->heater_start_x10) ? 1u : 0u;
}

/* 推进加热需求、资格确认和故障处置状态。 */
static void service_heater(const bms_afe_feature_snapshot_t *s,
                           const bms_user_params_t *config)
{
    uint8_t present = 0u;
    uint8_t source_known;
    if (!bms_board_heater_supported())
    {
        heater_idle();
        return;
    }

    if (!heater_circuit_safe(s)) return;

    if (config == 0 || !config->heater_enable ||
        !s->battery_temp_valid || heater_hard_fault())
    {
        heater_idle();
        return;
    }

    if (g_bms_report.discharge_current_a10 != 0u)
    {
        heater_idle();
        return;
    }

    if (!heater_demand(s, config))
    {
        heater_idle();
        return;
    }

    source_known = 0u;
#if BMS_HEATER_REQUIRE_CHARGE_SOURCE_PRESENT
    source_known = bms_afe_get_charge_source_present(&present);
    /* D011 有片内 C+ 检测：禁止靠旧会话在拔除/检测失效后继续耗电加热。 */
    if (!source_known || !present) {
        heater_idle();
        return;
    }
#else
    if (s_feature.heater_state == BMS_HEATER_IDLE)
        source_known = bms_afe_get_charge_source_present(&present);
#endif
    if (s_feature.heater_state == BMS_HEATER_IDLE)
    {
        /* 零电流可维持既有预热会话，不能证明仍有充电器来启动新周期。
         * 进入 ARMING 后，零电流才是充电路径已阻断的预期证据。
         * D008 充电源用于加热启动；D011 的在位门禁在上面持续检查。PB1 不是充电源。 */
        if (g_bms_report.charge_current_a10 != 0u ||
            (source_known && present))
        {
            s_feature.heater_state = BMS_HEATER_ARMING;
        }
        set_heater(0u);
        return;
    }

    if (s_feature.heater_state == BMS_HEATER_ARMING)
    {
        if (g_bms_report.charge_current_a10 != 0u)
        {
            set_heater(0u);
            return;
        }
        set_heater(1u);
        s_feature.heater_state = BMS_HEATER_ACTIVE;
        return;
    }

    set_heater(1u);
}
#endif

/* 发布均衡通道和工作状态到公共报告。 */
#if BMS_FEATURE_BALANCE_SUPPORTED
static void publish_balance(uint32_t mask)
{
    g_bms_report.balance_bits_low = (uint16_t)(mask & 0xFFFFu);
    g_bms_report.balance_bits_high = (uint16_t)((mask >> 16) & 0x00FFu);
    g_bms_system_status.bits.balance_status = mask ? 1u : 0u;
}

/* 应用均衡掩码并记录实际成功状态。 */
static uint8_t apply_balance_mask(uint32_t desired)
{
    uint32_t actual;

#if BMS_DEBUG_LOG_ENABLE
    if (s_feature.balance_requested_mask != desired)
        BMS_LOG(BMS_LOG_INFO, BMS_LOG_FEATURE, BMS_LOG_BALANCE_REQUEST, s_feature.balance_requested_mask, desired);
    s_feature.balance_requested_mask = desired;
#endif
    if (!bms_afe_set_balance_mask(desired))
    {
        BMS_LOG(BMS_LOG_ERROR, BMS_LOG_FEATURE, BMS_LOG_BALANCE_RESULT, desired, UINT32_MAX);
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        if (bms_afe_get_balance_mask(&actual)) publish_balance(actual);
        s_feature.balance_off_pending = 1u;
        return 0u;
    }

    /* AFE set 返回成功已经包含实际寄存器回读匹配。
     * 总线静默和未确认的 OFF 均返回失败；不再进行第二次同值回读。 */
    publish_balance(desired);
    if (desired == 0u) s_feature.balance_off_pending = 0u;
    bms_error_clear(BMS_ERROR_BALANCE);
    return 1u;
}

/* 检查电芯电压快照是否可用于均衡策略。 */
static uint8_t balance_sample_plausible(const bms_afe_feature_snapshot_t *s)
{
    uint16_t vmin = 0xFFFFu;
    uint16_t vmax = 0u;
    uint8_t step_ok = 1u;
    uint8_t i;

    if ((s->cell_count == 0u) ||
        (s->cell_count > BMS_AFE_FEATURE_MAX_CELLS))
    {
        s_feature.balance_prev_cell_count = 0u;
        return 0u;
    }

    for (i = 0u; i < s->cell_count; ++i)
    {
        uint16_t cell = g_bms_report.cell_voltage_mv[i];
        if (cell < BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV ||
            cell > BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV)
        {
            s_feature.balance_prev_cell_count = 0u;
            return 0u;
        }

        if (s_feature.balance_prev_cell_count == s->cell_count)
        {
            uint16_t previous = s_feature.balance_prev_cell_mv[i];
            uint16_t step = (cell >= previous) ?
                            (uint16_t)(cell - previous) :
                            (uint16_t)(previous - cell);
            if (step > BMS_BALANCE_CELL_MAX_STEP_MV) step_ok = 0u;
        }

        if (cell < vmin) vmin = cell;
        if (cell > vmax) vmax = cell;
    }

    if ((uint16_t)(vmax - vmin) > BMS_BALANCE_SUSPECT_DELTA_MV ||
        g_bms_report.cell_min_mv != vmin ||
        g_bms_report.cell_max_mv != vmax ||
        g_bms_report.cell_delta_mv != (uint16_t)(vmax - vmin))
    {
        s_feature.balance_prev_cell_count = 0u;
        return 0u;
    }

    /* 大跳变本帧不可信，但保留合理新基线，防止永远和旧值比较。 */
    for (i = 0u; i < s->cell_count; ++i)
        s_feature.balance_prev_cell_mv[i] = g_bms_report.cell_voltage_mv[i];
    s_feature.balance_prev_cell_count = s->cell_count;
    return step_ok;
}

/* 更新均衡电压可信度及连续确认状态。 */
static void update_balance_voltage_trust(const bms_afe_feature_snapshot_t *s)
{
    uint16_t required = (uint16_t)BMS_BALANCE_TRUST_CONFIRM_SAMPLES;

    if (required == 0u) required = 1u;

    if (!balance_sample_plausible(s))
    {
        s_feature.balance_trust_samples = 0u;
        return;
    }

    if (s_feature.balance_trust_samples < required)
        ++s_feature.balance_trust_samples;
}

/* 确认计数是唯一资格状态；不再维护必须同步置位/清零的布尔副本。 */
static uint8_t balance_voltage_trusted(void)
{
    uint16_t required = (uint16_t)BMS_BALANCE_TRUST_CONFIRM_SAMPLES;
    if (required == 0u) required = 1u;
    return s_feature.balance_trust_samples >= required;
}

/* 检查温度有效性与均衡允许温区。 */
static uint8_t balance_temperature_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t charge_ot_recover;
    uint16_t charge_ut_recover;
    uint16_t mos_ot_recover;

    if (!s->battery_temp_valid || !s->mos_temp_valid) return 0u;

#if BMS_BALANCE_TEMP_STOP_X10 != 0
    {
        uint16_t hottest_x10 = (s->battery_temp_max_x10 > s->mos_temp_x10) ?
                              s->battery_temp_max_x10 : s->mos_temp_x10;
        if (hottest_x10 >= BMS_BALANCE_TEMP_STOP_X10)
            s_feature.balance_temperature_paused = 1u;
        else if (hottest_x10 < BMS_BALANCE_TEMP_RESUME_X10)
            s_feature.balance_temperature_paused = 0u;
        if (s_feature.balance_temperature_paused) return 0u;
    }
#endif

    /*
     * 均衡会产生热量，使用既有保护恢复边界作为保守准入窗口，
     * 不依赖零电流时可能清除的方向性故障位；阈值零表示相应保护关闭。
     */
    charge_ot_recover = g_bms_protection_params.charge_otp_recover_x10;
    charge_ut_recover = g_bms_protection_params.charge_utp_recover_x10;
    mos_ot_recover = g_bms_protection_params.mos_otp_recover_x10;

    if ((charge_ot_recover != 0u) &&
        (s->battery_temp_max_x10 >= charge_ot_recover))
        return 0u;
    if ((charge_ut_recover != 0u) &&
        (s->battery_temp_min_x10 <= charge_ut_recover))
        return 0u;
    if ((mos_ot_recover != 0u) &&
        (s->mos_temp_x10 >= mos_ot_recover))
        return 0u;
    return 1u;
}

/* 检查必须关闭均衡的硬故障条件。 */
static uint8_t balance_hard_fault(void)
{
    const bms_fault_bits_t *f = &g_bms_report.fault_third.bits;

    /* 有意不列入单体过压：充电已阻断时，确认的被动泄放是高单体的合法恢复路径。 */
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            f->cell_uvp || f->pack_uvp || f->pack_ovp ||
            f->charge_ocp || f->discharge_ocp ||
            f->charge_otp || f->charge_utp ||
            f->discharge_otp || f->discharge_utp ||
            f->mos_otp) ? 1u : 0u;
}

/* 每串独立判断准入；有限额时按电压竞争，旧通道只享有有界切换回差。 */
static uint32_t select_balance_mask(const bms_afe_feature_snapshot_t *s,
                                    const bms_user_params_t *config)
{
    /* 仅在信任资格成立且没有待确认OFF时进入；回读就是唯一活动通道来源。 */
    uint32_t active = reported_balance_mask();
    uint32_t candidates = 0u;
    uint8_t i;
    for (i = 0u; i < s->cell_count; ++i)
    {
        uint16_t cell = g_bms_report.cell_voltage_mv[i];
        uint16_t delta;
        uint32_t bit = 1uL << i;
        if (cell < config->balance_start_mv || cell < g_bms_report.cell_min_mv) continue;
        delta = (uint16_t)(cell - g_bms_report.cell_min_mv);
        if (active & bit) {
            if (delta > config->balance_stop_delta_mv) candidates |= bit;
        } else if (delta >= config->balance_start_delta_mv) {
            candidates |= bit;
        }
    }

#if BMS_BALANCE_MAX_CELLS == 0
    return candidates;
#else
    {
        uint32_t selected = 0u;
        uint16_t switch_delta_mv = (uint16_t)(config->balance_start_delta_mv -
                                             config->balance_stop_delta_mv);
        uint8_t count;
        /* Config 已校验 start > stop。复用两者差值，避免电压交叉时每帧换串。
         * 新通道必须比旧通道高出超过回差才可替换；不增加参数或跨轮状态。 */
        for (count = 0u; count < BMS_BALANCE_MAX_CELLS && candidates != 0u; ++count)
        {
            uint8_t best = s->cell_count;
            uint8_t best_active = 0u;
            uint32_t highest_priority_mv = 0u;
            for (i = 0u; i < s->cell_count; ++i) {
                uint32_t bit = 1uL << i;
                uint32_t priority_mv;
                uint8_t cell_active;
                if (!(candidates & bit)) continue;
                cell_active = (active & bit) ? 1u : 0u;
                priority_mv = (uint32_t)g_bms_report.cell_voltage_mv[i] +
                              (cell_active ? switch_delta_mv : 0u);
                /* 回差边界优先旧通道；同状态同电压按较小串号稳定选择。 */
                if (priority_mv > highest_priority_mv ||
                    (priority_mv == highest_priority_mv && cell_active && !best_active)) {
                    best = i;
                    best_active = cell_active;
                    highest_priority_mv = priority_mv;
                }
            }
            if (best == s->cell_count) break;
            selected |= 1uL << best;
            candidates &= ~(1uL << best);
        }
        return selected;
    }
#endif
}

/*
 * 统一均衡资格与 mask 仲裁；只使用有效 cell，
 * 失效测量和硬故障应通过现有路径停止均衡。
 */
static void service_balance(const bms_afe_feature_snapshot_t *s,
                            const bms_user_params_t *config)
{
    uint32_t desired = 0u;
    /* 即使其他门禁阻断，也持续记录过热/降温，不能丢失暂停回差。 */
    uint8_t temperature_safe = balance_temperature_safe(s);
    uint8_t allowed;

    /* 冷启动静置可均衡；只检查当前放电，不保留充电经历。 */
    allowed = (uint8_t)(s->cell_count > 0u && s->cell_count <= BMS_AFE_FEATURE_MAX_CELLS &&
                        !s_feature.balance_off_pending &&
                        config != 0 && config->balance_enable &&
                        bms_board_balance_supported() &&
                        balance_voltage_trusted() &&
#if BMS_FEATURE_HEATER_SUPPORTED
                        (s_feature.heater_state == BMS_HEATER_IDLE) &&
#endif
                        g_bms_report.discharge_current_a10 == 0u &&
                        temperature_safe &&
                        !balance_hard_fault());

    if (allowed) desired = select_balance_mask(s, config);

    if (!apply_balance_mask(desired))
    {
        /* 非零应用失败后仅追加一次关闭尝试；关闭未确认时以后只重试 OFF。
         * 受 guard 总线静默约束，不在这里重初始化 AFE，也不忙等。 */
        if (desired != 0u) (void)apply_balance_mask(0u);
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
    }
}
#endif

/* 复位加热和均衡的运行资格。 */
void bms_features_init(void)
{
    /* AFE 重初始化只撤销运行资格；保留熔断、温度暂停和未确认关闭状态。 */
#if BMS_FEATURE_HEATER_SUPPORTED
    /* 不可逆动作在本次 MCU 启动中锁存，AFE 重初始化不能重新授权。 */
    s_feature.heater_state = BMS_HEATER_IDLE;
    s_feature.heater_off_hot_samples = 0u;
#endif
#if BMS_FEATURE_BALANCE_SUPPORTED
    /* 温度暂停和未确认关闭状态保留，不能用重初始化绕过恢复条件。 */
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
#if BMS_DEBUG_LOG_ENABLE
    s_feature.balance_requested_mask = 0u;
#endif
#endif
    /* prev_cell_count 为零时不读旧数组；完整有效帧才建立新基线。 */
#if BMS_FEATURE_HEATER_SUPPORTED
    bms_board_features_init(s_feature.heater_fuse_fired);
    bms_board_heater_set(0u);
    g_bms_system_status.bits.heater_status = 0u;
#endif
#if BMS_FEATURE_BALANCE_SUPPORTED
    publish_balance(0u);
#endif
}

/* 成功取得的栈上快照只在本次同步调用中使用，内部 helper 不重复判空/判有效。 */
void bms_features_service(void)
{
#if BMS_FEATURE_HEATER_SUPPORTED || BMS_FEATURE_BALANCE_SUPPORTED
    bms_afe_feature_snapshot_t s;
    bms_user_params_t config;
    const bms_user_params_t *user;

    if (!bms_afe_get_feature_snapshot(&s))
    {
        bms_features_stop();
        return;
    }

    user = bms_config_get_user(&config) ? &config : 0;
#if BMS_FEATURE_BALANCE_SUPPORTED
    update_balance_voltage_trust(&s);
#endif
#if BMS_FEATURE_HEATER_SUPPORTED
    service_heater(&s, user);
#endif
#if BMS_FEATURE_BALANCE_SUPPORTED
    service_balance(&s, user);
#endif
#endif
}

/* 主动关闭输出和 AFE 失效均撤销加热、均衡资格，硬件关闭仍需确认。 */
void bms_features_stop(void)
{
#if BMS_FEATURE_HEATER_SUPPORTED
    heater_idle();
    s_feature.heater_off_hot_samples = 0u;
#endif
#if BMS_FEATURE_BALANCE_SUPPORTED
#if BMS_DEBUG_LOG_ENABLE
    s_feature.balance_requested_mask = 0u;
#endif
    s_feature.balance_off_pending = 1u;
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
    /*
     * 输出撤销时物理均衡状态可能未知；不能把期望关闭状态当成硬件反馈。
     * 保留最后回读值至通信恢复，DVC 独立均衡定时器仍作硬件回退。
     */
    if (reported_balance_mask() != 0u)
    {
        if (!bms_error_get(BMS_ERROR_BALANCE))
            bms_error_raise(BMS_ERROR_BALANCE);
    }
#endif

#if BMS_FEATURE_HEATER_SUPPORTED
    if (s_feature.heater_fuse_fired && !bms_error_get(BMS_ERROR_HEAT))
        bms_error_raise(BMS_ERROR_HEAT);
#endif
}

/* 查询充电方向相关的公共功能阻断。 */
uint8_t bms_features_charge_direction_blocked(void)
{
#if BMS_FEATURE_HEATER_SUPPORTED
    return (s_feature.heater_state == BMS_HEATER_ARMING ||
            s_feature.heater_state == BMS_HEATER_ACTIVE) ? 1u : 0u;
#else
    return 0u;
#endif
}
