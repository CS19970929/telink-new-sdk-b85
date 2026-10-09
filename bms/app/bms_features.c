/*
 * 文件功能：公共均衡、Open-Wire 与 heater 策略；依据采样可信度、温度和保护状态仲裁，
 * 硬件动作交给 backend。
 * bms/app/bms_features.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#include "bms_features.h"
#include "bms_config_store.h"

#include "bms_board.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_parameters.h"
#include <string.h>

#define BMS_OPENWIRE_FIRST_IDLE_SAMPLES \
    ((BMS_OPENWIRE_FIRST_IDLE_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)
#define BMS_BALANCE_TRUST_CONFIRM_SAMPLES \
    ((BMS_BALANCE_TRUST_CONFIRM_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)

typedef struct {
    uint8_t heater_on;
    uint8_t heater_fuse_fired;
    bms_heater_state_t heater_state;
    uint16_t heater_off_hot_samples;

    uint8_t charge_session_active;

    uint8_t openwire_active;
    uint8_t openwire_sample_active;
    uint8_t openwire_suspected;
    uint8_t openwire_cell_count;
    uint16_t openwire_idle_samples;
    bms_afe_openwire_result_t openwire_result;
    uint8_t openwire_cleanup_pending;
    uint8_t openwire_state;
    uint8_t openwire_last_error;
    uint8_t openwire_failure_error;
    uint32_t openwire_sequence;
    uint32_t openwire_started_tick;
    uint32_t openwire_finished_tick;
    uint32_t openwire_healthy_tick;
    uint32_t openwire_failure_tick;
    uint32_t openwire_wait_tick;
    uint32_t openwire_wait_ms;
    uint32_t openwire_attempts;
    uint32_t openwire_completed;
    uint32_t openwire_failed;
    uint32_t openwire_latched_mask; /* 非零即已确认故障；只由完整结果更新，不另存布尔锁存。 */

    uint8_t balance_active;
    uint16_t balance_trust_samples;
    uint8_t balance_prev_cell_count;
    uint16_t balance_prev_cell_mv[BMS_AFE_FEATURE_MAX_CELLS];
    uint32_t balance_requested_mask;
} bms_feature_state_t;

/*
 * 公共功能的资格/阶段状态；仅由 service/init/AFE 失效处理入口更新，
 * 产品能力由配置提供。
 */
static bms_feature_state_t s_feature;

/* 汇总板级与 AFE 可确认的充电源证据。 */
static uint8_t charge_source_present(void)
{
    uint8_t present = 0u;
    if (bms_afe_get_charge_source_present(&present)) return present ? 1u : 0u;
    return bms_board_charge_source_present() ? 1u : 0u;
}

/* 充电会话的唯一运行更新入口；先于 Open-Wire/Heater/均衡执行。 */
static void update_charge_session(void)
{
    /*
     * D008 无电流检测前的 AFE 硬件充电低温关断，因此可信充电电流用于建立会话。
     * 预热主动阻断 CHG 后电流消失，零电流不能清会话；
     * 真实放电方向是退出充电场景的可信证据。
     */
    if (g_bms_report.discharge_current_a10 != 0u)
        s_feature.charge_session_active = 0u;
    else if ((g_bms_report.charge_current_a10 != 0u) || charge_source_present())
        s_feature.charge_session_active = 1u;
}

/* 更新加热请求并驱动支持的板级输出。 */
static void set_heater(uint8_t on)
{
    on = (on && bms_board_heater_supported()) ? 1u : 0u;
    bms_board_heater_set(on);
    s_feature.heater_on = on;
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

    if ((s == 0) || !s->heater_temp_valid)
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

    if (s_feature.heater_on)
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
            (s_feature.openwire_latched_mask != 0u) ||
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

    if ((s == 0) || (config == 0) || !s->battery_temp_valid) return 0u;

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
static void service_heater(const bms_afe_feature_snapshot_t *s)
{
    bms_user_params_t config;

    if ((s == 0) || !s->valid || !bms_board_heater_supported())
    {
        heater_idle();
        return;
    }

    if (!heater_circuit_safe(s)) return;

    if (!bms_config_get_user(&config) || !config.heater_enable ||
        !s->battery_temp_valid || heater_hard_fault())
    {
        heater_idle();
        return;
    }

    if (!s_feature.charge_session_active)
    {
        heater_idle();
        return;
    }

    /* 检测不写 MOS；温度保护照常评估，暂缓新加热周期。 */
    if (s_feature.openwire_active)
    {
        set_heater(0u);
        return;
    }

    if (!heater_demand(s, &config))
    {
        heater_idle();
        return;
    }

    if (s_feature.heater_state == BMS_HEATER_IDLE)
    {
        /* 零电流可维持既有预热会话，不能证明仍有充电器来启动新周期。
         * 进入 ARMING 后，零电流才是充电路径已阻断的预期证据。 */
        if ((g_bms_report.charge_current_a10 != 0u) || charge_source_present())
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

/* 检查禁止断线检测的硬故障条件。 */
static uint8_t openwire_hard_fault(void)
{
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG)) ? 1u : 0u;
}

/* 检查采样、负载和保护状态是否允许断线检测。 */
static uint8_t openwire_eligible(void)
{
    if (s_feature.heater_state != BMS_HEATER_IDLE) return 0u;
    if (g_bms_report.charge_current_a10 || g_bms_report.discharge_current_a10) return 0u;
    return openwire_hard_fault() ? 0u : 1u;
}

/* 发布均衡通道和工作状态到公共报告。 */
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

    if (s_feature.balance_requested_mask != desired)
        BMS_LOG(BMS_LOG_INFO, BMS_LOG_FEATURE, BMS_LOG_BALANCE_REQUEST, s_feature.balance_requested_mask, desired);
    s_feature.balance_requested_mask = desired;
    if (!bms_afe_set_balance_mask(desired))
    {
        BMS_LOG(BMS_LOG_ERROR, BMS_LOG_FEATURE, BMS_LOG_BALANCE_RESULT, desired, UINT32_MAX);
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        if (bms_afe_get_balance_mask(&actual)) publish_balance(actual);
        return 0u;
    }

    if (!bms_afe_get_balance_mask(&actual))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        return 0u;
    }

    bms_error_clear(BMS_ERROR_BALANCE);
    publish_balance(actual);
    return 1u;
}

/* 32 kHz SDK tick 实际为每毫秒 32 个计数；无符号差值允许一次回绕。 */
static uint32_t openwire_wait_remaining(uint32_t now)
{
    uint32_t elapsed = (uint32_t)(now - s_feature.openwire_wait_tick) / 32u;
    return elapsed < s_feature.openwire_wait_ms ? s_feature.openwire_wait_ms - elapsed : 0u;
}

static void openwire_put32(uint16_t *p, uint32_t v)
{
    p[0] = (uint16_t)v; p[1] = (uint16_t)(v >> 16);
}

/* 独立只读快照，无 Flash 写入，也不访问 AFE。字段定义见 OPENWIRE_MONITOR.md。 */
static void publish_openwire(void)
{
    uint16_t w[BMS_DIAG_OPENWIRE_WORDS] = {0};
    uint32_t now = bms_diag_tick();
    w[0] = 0x4F57u; w[1] = 1u; w[2] = BMS_AFE_BACKEND;
    w[3] = s_feature.openwire_state;
    w[4] = (uint16_t)(s_feature.openwire_active | (s_feature.openwire_suspected << 1) |
        ((s_feature.openwire_latched_mask != 0u) << 2) | (s_feature.openwire_cleanup_pending << 3) |
        (s_feature.openwire_result.valid << 4) | (s_feature.openwire_result.determinate << 5));
    w[5] = s_feature.openwire_last_error;
    openwire_put32(w+6, s_feature.openwire_sequence);
    openwire_put32(w+8, now);
    openwire_put32(w+10, s_feature.openwire_started_tick);
    openwire_put32(w+12, s_feature.openwire_finished_tick);
    openwire_put32(w+14, s_feature.openwire_healthy_tick);
    openwire_put32(w+16, s_feature.openwire_attempts);
    openwire_put32(w+18, s_feature.openwire_completed);
    openwire_put32(w+20, s_feature.openwire_failed);
    openwire_put32(w+22, s_feature.openwire_latched_mask);
    openwire_put32(w+24, s_feature.openwire_result.open_cell_mask);
    openwire_put32(w+26, s_feature.openwire_result.raw_phase[0]);
    openwire_put32(w+28, s_feature.openwire_result.raw_phase[1]);
    w[30] = s_feature.openwire_result.phase_coverage;
    w[31] = s_feature.openwire_cell_count;
    openwire_put32(w+32, openwire_wait_remaining(now));
    openwire_put32(w+34, s_feature.openwire_active ? (uint32_t)(now-s_feature.openwire_started_tick)/32u : 0u);
    openwire_put32(w+36, s_feature.openwire_failure_tick);
    w[38] = s_feature.openwire_failure_error;
    w[39] = (uint16_t)((g_bms_report.charge_current_a10 || g_bms_report.discharge_current_a10 ? 1u : 0u) |
        (s_feature.heater_state != BMS_HEATER_IDLE ? 2u : 0u) | (openwire_hard_fault() ? 4u : 0u) |
        (bms_error_get(BMS_ERROR_BALANCE) ? 8u : 0u));
    openwire_put32(w+40, BMS_OPENWIRE_PERIOD_MS);
    openwire_put32(w+42, BMS_DIAG_BUILD_ID);
    openwire_put32(w+46, s_feature.openwire_attempts ?
        (uint32_t)(s_feature.openwire_finished_tick-s_feature.openwire_started_tick)/32u : 0u);
    bms_diag_openwire(w);
}

/* 所有退出都撤销激励并校验；超时不等于断线，清理 I/O 故障由 guard 仲裁。 */
static void finish_openwire(uint8_t error)
{
    uint32_t now = bms_diag_tick();
    if (error == BMS_OW_ERR_NONE && s_feature.openwire_result.open_cell_mask != 0u) {
        s_feature.openwire_latched_mask = s_feature.openwire_result.open_cell_mask;
    }
    s_feature.openwire_active = 0u;
    s_feature.openwire_cleanup_pending = 1u;
    if (bms_afe_openwire_stop()) s_feature.openwire_cleanup_pending = 0u;
    else error = BMS_OW_ERR_CLEANUP;
    s_feature.openwire_finished_tick = now;
    s_feature.openwire_wait_tick = now;
    s_feature.openwire_idle_samples = 0u;
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
    if (error != BMS_OW_ERR_NONE) {
        ++s_feature.openwire_failed;
        s_feature.openwire_suspected = 1u;
        s_feature.openwire_last_error = error;
        s_feature.openwire_failure_error = error;
        s_feature.openwire_failure_tick = now;
        s_feature.openwire_state = s_feature.openwire_cleanup_pending ? BMS_OW_CLEANUP : BMS_OW_FAILED;
        s_feature.openwire_wait_ms = BMS_OPENWIRE_RETRY_MS;
    } else {
        ++s_feature.openwire_completed;
        s_feature.openwire_last_error = BMS_OW_ERR_NONE;
        s_feature.openwire_latched_mask = s_feature.openwire_result.open_cell_mask;
        s_feature.openwire_suspected = (s_feature.openwire_latched_mask != 0u);
        s_feature.openwire_state = (s_feature.openwire_latched_mask != 0u) ? BMS_OW_FAULT : BMS_OW_HEALTHY;
        if (s_feature.openwire_latched_mask == 0u) s_feature.openwire_healthy_tick = now;
        s_feature.openwire_wait_ms = (s_feature.openwire_latched_mask != 0u) ? BMS_OPENWIRE_RETRY_MS : BMS_OPENWIRE_PERIOD_MS;
    }
    ++s_feature.openwire_sequence;
}

/* 检测过程不关 MOS；仅完整有效的诊断结果更新断线保护锁存。 */
static void service_openwire(void)
{
    bms_afe_diag_state_t state;
    bms_afe_openwire_result_t result;
    uint32_t now = bms_diag_tick();
    if (s_feature.openwire_active) {
        if (!openwire_eligible()) { finish_openwire(BMS_OW_ERR_INTERRUPTED); return; }
        memset(&result, 0, sizeof(result));
        state = bms_afe_openwire_poll(&result);
        if (state == BMS_AFE_DIAG_READY) {
            s_feature.openwire_result = result;
            finish_openwire(result.valid && result.determinate ? BMS_OW_ERR_NONE : BMS_OW_ERR_INCOMPLETE);
        } else if (state == BMS_AFE_DIAG_ERROR || state == BMS_AFE_DIAG_IDLE) {
            s_feature.openwire_result = result;
            finish_openwire(result.error ? result.error : BMS_OW_ERR_IO);
        } else if ((uint32_t)(now-s_feature.openwire_started_tick) >= BMS_OPENWIRE_TIMEOUT_MS * 32u) {
            s_feature.openwire_result = result;
            finish_openwire(BMS_OW_ERR_TIMEOUT);
        } else {
            if (result.phase_coverage != s_feature.openwire_result.phase_coverage)
                ++s_feature.openwire_sequence;
            s_feature.openwire_result = result;
        }
        return;
    }
    /* 可疑电压不能绕过失败退避；清理失败也只在退避后重试。 */
    if (openwire_wait_remaining(now) != 0u) return;
    if (s_feature.openwire_cleanup_pending) {
        if (!bms_afe_openwire_stop()) {
            s_feature.openwire_wait_tick = now;
            s_feature.openwire_wait_ms = BMS_OPENWIRE_RETRY_MS;
            return;
        }
        s_feature.openwire_cleanup_pending = 0u;
        s_feature.openwire_sample_active = 1u;
        s_feature.openwire_state = BMS_OW_FAILED;
        ++s_feature.openwire_sequence;
        return; /* 下次正常转换后才允许新检测。 */
    }
    if (!openwire_eligible()) { s_feature.openwire_idle_samples = 0u; return; }
    if (!s_feature.openwire_suspected &&
        s_feature.openwire_idle_samples < (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES) {
        ++s_feature.openwire_idle_samples;
        return;
    }
    s_feature.balance_active = 0u;
    if (!apply_balance_mask(0u)) {
        s_feature.openwire_wait_tick = now;
        s_feature.openwire_wait_ms = BMS_OPENWIRE_RETRY_MS;
        return;
    }
    ++s_feature.openwire_attempts;
    ++s_feature.openwire_sequence;
    s_feature.openwire_started_tick = now;
    s_feature.openwire_finished_tick = now;
    s_feature.openwire_idle_samples = 0u;
    s_feature.openwire_sample_active = 1u;
    memset(&s_feature.openwire_result, 0, sizeof(s_feature.openwire_result));
    if (bms_afe_openwire_start()) {
        s_feature.openwire_active = 1u;
        s_feature.openwire_state = BMS_OW_RUNNING;
        s_feature.openwire_last_error = BMS_OW_ERR_NONE;
    } else finish_openwire(BMS_OW_ERR_START);
}

/* 检查电芯电压快照是否可用于均衡策略。 */
static uint8_t balance_sample_plausible(const bms_afe_feature_snapshot_t *s)
{
    uint16_t vmin = 0xFFFFu;
    uint16_t vmax = 0u;
    uint8_t i;

    if ((s == 0) || !s->valid || (s->cell_count == 0u) ||
        (s->cell_count > BMS_AFE_FEATURE_MAX_CELLS))
        return 0u;

    for (i = 0u; i < s->cell_count; ++i)
    {
        uint16_t cell = g_bms_report.cell_voltage_mv[i];
        if (cell < BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV ||
            cell > BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV)
            return 0u;

        if (s_feature.balance_prev_cell_count == s->cell_count)
        {
            uint16_t previous = s_feature.balance_prev_cell_mv[i];
            uint16_t step = (cell >= previous) ?
                            (uint16_t)(cell - previous) :
                            (uint16_t)(previous - cell);
            if (step > BMS_BALANCE_CELL_MAX_STEP_MV) return 0u;
        }

        if (cell < vmin) vmin = cell;
        if (cell > vmax) vmax = cell;
    }

    if ((uint16_t)(vmax - vmin) > BMS_BALANCE_SUSPECT_DELTA_MV) return 0u;
    if (g_bms_report.cell_min_mv != vmin ||
        g_bms_report.cell_max_mv != vmax ||
        g_bms_report.cell_delta_mv != (uint16_t)(vmax - vmin))
        return 0u;

    for (i = 0u; i < s->cell_count; ++i)
        s_feature.balance_prev_cell_mv[i] = g_bms_report.cell_voltage_mv[i];
    s_feature.balance_prev_cell_count = s->cell_count;
    return 1u;
}

/* 更新均衡电压可信度及连续确认状态。 */
static void update_balance_voltage_trust(const bms_afe_feature_snapshot_t *s)
{
    uint16_t required = (uint16_t)BMS_BALANCE_TRUST_CONFIRM_SAMPLES;

    if (required == 0u) required = 1u;

    if (!balance_sample_plausible(s))
    {
        s_feature.balance_trust_samples = 0u;
        s_feature.balance_active = 0u;
        s_feature.openwire_suspected = 1u;
        return;
    }

    if (s_feature.openwire_suspected || (s_feature.openwire_latched_mask != 0u))
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

    if ((s == 0) || !s->battery_temp_valid || !s->mos_temp_valid) return 0u;

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

/*
 * 统一均衡资格与 mask 仲裁；只使用有效 cell，
 * 失效测量和硬故障应通过现有路径停止均衡。
 */
static void service_balance(const bms_afe_feature_snapshot_t *s)
{
    bms_user_params_t config;
    uint16_t threshold;
    uint32_t desired = 0u;
    uint8_t i;
    uint8_t allowed;

    allowed = (uint8_t)((s != 0) && s->valid &&
                        bms_config_get_user(&config) &&
                        config.balance_enable &&
                        bms_board_balance_supported() &&
                        balance_voltage_trusted() &&
                        !s_feature.openwire_active &&
                        (s_feature.openwire_latched_mask == 0u) &&
                        !s_feature.openwire_suspected &&
                        (s_feature.heater_state == BMS_HEATER_IDLE) &&
                        s_feature.charge_session_active &&
                        balance_temperature_safe(s) &&
                        !balance_hard_fault());

    if (allowed)
    {
        threshold = s_feature.balance_active ?
                    config.balance_stop_delta_mv :
                    config.balance_start_delta_mv;

        if (g_bms_report.cell_max_mv >= config.balance_start_mv &&
            g_bms_report.cell_delta_mv >= threshold)
        {
            for (i = 0u; i < s->cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i)
            {
                uint16_t cell = g_bms_report.cell_voltage_mv[i];
                if (cell >= config.balance_start_mv &&
                    cell >= g_bms_report.cell_min_mv &&
                    (uint16_t)(cell - g_bms_report.cell_min_mv) >= threshold)
                    desired |= (1uL << i);
            }
        }
    }

    if (apply_balance_mask(desired))
        s_feature.balance_active = desired ? 1u : 0u;
    else
        s_feature.balance_active = 0u;
}

/* 复位加热、均衡和断线检测的公共状态。 */
void bms_features_init(void)
{
    /* AFE 重初始化只撤销运行资格，保留故障、原始结果和本次启动累计计数。
     * MCU 冷启动由静态 RAM 零初始化；不能用 AFE 重新初始化冒充故障解除。 */
    s_feature.heater_on = 0u;
    s_feature.heater_fuse_fired = 0u;
    s_feature.heater_state = BMS_HEATER_IDLE;
    s_feature.heater_off_hot_samples = 0u;
    s_feature.charge_session_active = 0u;
    s_feature.balance_active = 0u;
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
    s_feature.balance_requested_mask = 0u;
    memset(s_feature.balance_prev_cell_mv, 0, sizeof(s_feature.balance_prev_cell_mv));
    if (s_feature.openwire_active) {
        ++s_feature.openwire_failed;
        s_feature.openwire_failure_error = BMS_OW_ERR_SAMPLE;
        s_feature.openwire_failure_tick = bms_diag_tick();
        s_feature.openwire_finished_tick = s_feature.openwire_failure_tick;
    }
    s_feature.openwire_active = 0u;
    s_feature.openwire_sample_active = 0u;
    s_feature.openwire_cleanup_pending = 0u;
    s_feature.openwire_idle_samples = 0u;
    s_feature.openwire_state = (s_feature.openwire_latched_mask != 0u) ? BMS_OW_FAULT : BMS_OW_WAIT;
    ++s_feature.openwire_sequence;
    s_feature.openwire_wait_tick = bms_diag_tick();
    s_feature.openwire_wait_ms = BMS_OPENWIRE_FIRST_IDLE_MS;
    bms_sw_protection_init();
    bms_board_features_init();
    bms_board_heater_set(0u);
    g_bms_system_status.bits.heater_status = 0u;
    publish_balance(0u);
    publish_openwire();
}

/* 按有效快照推进加热、断线检测及均衡策略。 */
void bms_features_service(void)
{
    bms_afe_feature_snapshot_t s;

    /* 轮询可能在本帧采样后完成 COW 并清 active；下次采样前不能把该诊断电压交给 SOC。 */
    s_feature.openwire_sample_active = s_feature.openwire_active;
    memset(&s, 0, sizeof(s));
    if (!bms_afe_get_feature_snapshot(&s) || !s.valid)
    {
        bms_features_on_afe_invalid();
        return;
    }

    update_charge_session();
    if (s.cell_count != 0u && s.cell_count <= BMS_AFE_FEATURE_MAX_CELLS)
        s_feature.openwire_cell_count = s.cell_count;
    if (!s_feature.openwire_sample_active && !s_feature.openwire_cleanup_pending)
        update_balance_voltage_trust(&s);
    service_openwire();
    service_heater(&s);
    service_balance(&s);
    publish_openwire();
}

/* AFE 样本失效时撤销功能资格并停止相关输出。 */
void bms_features_on_afe_invalid(void)
{
    heater_idle();
    s_feature.heater_off_hot_samples = 0u;
    s_feature.charge_session_active = 0u;
    s_feature.balance_requested_mask = 0u;
    s_feature.balance_active = 0u;
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
    if (s_feature.openwire_active) {
        s_feature.openwire_cleanup_pending = 1u;
        s_feature.openwire_state = BMS_OW_CLEANUP;
        s_feature.openwire_last_error = BMS_OW_ERR_SAMPLE;
        s_feature.openwire_failure_error = BMS_OW_ERR_SAMPLE;
        s_feature.openwire_failure_tick = bms_diag_tick();
        s_feature.openwire_finished_tick = s_feature.openwire_failure_tick;
        s_feature.openwire_wait_tick = s_feature.openwire_failure_tick;
        s_feature.openwire_wait_ms = BMS_OPENWIRE_RETRY_MS;
        ++s_feature.openwire_failed;
        ++s_feature.openwire_sequence;
    }
    s_feature.openwire_active = 0u;
    s_feature.openwire_suspected = 1u;
    s_feature.openwire_idle_samples = 0u;

    /*
     * AFE 总线失效时物理均衡状态未知；不能把期望关闭状态当成硬件反馈。
     * 保留最后回读值至通信恢复，DVC 独立均衡定时器仍作硬件回退。
     */
    if ((g_bms_report.balance_bits_low != 0u) ||
        (g_bms_report.balance_bits_high != 0u))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE))
            bms_error_raise(BMS_ERROR_BALANCE);
    }

    if (s_feature.heater_fuse_fired && !bms_error_get(BMS_ERROR_HEAT))
        bms_error_raise(BMS_ERROR_HEAT);
    publish_openwire();
}

/* 复制 feature 所有者的 RAM 状态，不额外读取总线或推进状态机。 */
void bms_features_get_status(bms_features_status_t *status)
{
    if (status == 0) return;
    status->heater_on = s_feature.heater_on;
    status->heater_fuse_fired = s_feature.heater_fuse_fired;
    status->heater_state = s_feature.heater_state;
    status->charge_session_active = s_feature.charge_session_active;
    status->balance_active = s_feature.balance_active;
    status->balance_voltage_trusted = balance_voltage_trusted();
    status->openwire_suspected = s_feature.openwire_suspected;
    status->openwire_active = s_feature.openwire_active;
    status->openwire_sample_active =
        (s_feature.openwire_active || s_feature.openwire_sample_active ||
         s_feature.openwire_cleanup_pending) ? 1u : 0u;
}

/* 已确认断线、参数无效或真实 AFE 清理故障未恢复时保持阻断；活动检测不阻断。 */
uint8_t bms_features_outputs_blocked(void)
{
    return (!bms_protection_params_valid() ||
            s_feature.openwire_cleanup_pending ||
            (s_feature.openwire_latched_mask != 0u)) ? 1u : 0u;
}

/* 查询充电方向相关的公共功能阻断。 */
uint8_t bms_features_charge_direction_blocked(void)
{
    return (s_feature.heater_state == BMS_HEATER_ARMING ||
            s_feature.heater_state == BMS_HEATER_ACTIVE) ? 1u : 0u;
}

/* 编码加热、均衡和断线检测的阻断原因。 */
uint32_t bms_features_diag_reasons(uint8_t charge)
{
    uint32_t reason = 0u;
    if (s_feature.openwire_cleanup_pending) reason |= DIAG_BLOCK_COMM;
    if (s_feature.openwire_latched_mask != 0u)
        reason |= DIAG_BLOCK_OPENWIRE;
    if (charge && bms_features_charge_direction_blocked())
        reason |= DIAG_BLOCK_HEATER;
    return reason;
}
