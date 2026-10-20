/* SOC 唯一电量所有者：安时积分、可信端点、静置 OCV 与限速显示。 */
#include "bms_soc.h"
#include "bms_product.h"
#include "bms_config_store.h"
#include "bms_soc_profile.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_parameters.h"
#include "bms_diag.h"
#include <string.h>

#define BMS_SOC_DIAG_STATE_ENABLE (BMS_SERVICE_DIAG_ENABLE || BMS_SOC_BOARD_TEST_ENABLE)
#define SOC_INTEGRAL_PERIOD_MS BMS_SAMPLE_PERIOD_MS
#define SOC_PERCENT_MAX 100u
#define SOC_CAPACITY_UNITS_PER_FACTORY 3600u
#define SOC_REPORT_CAPACITY_DIVISOR 360u
#define SOC_CYCLE_MAX 65535u
#define SOC_DSG_INT_MAX 99u
#define SOC_TICKS_PER_SECOND (1000u / BMS_SAMPLE_PERIOD_MS)
#define SOC_MINUTE_MS 60000u
#define SOC_MAX_CELLS 24u
#define SOC_INTEGRAL_DENOMINATOR 100000
/* 容量上限6553*3600=23590800；乘100仍在uint32_t内，TC32无需64位库。 */
#if BMS_SOC_CAPACITY_MAX_0P1AH > (0xFFFFFFFFu / SOC_CAPACITY_UNITS_PER_FACTORY / 100u)
#error SOC_capacity_exceeds_32_bit_arithmetic_bounds
#endif
#if BMS_SOC_MAX_SAMPLE_GAP_MS > 400u
#error SOC_sample_gap_exceeds_reviewed_integer_arithmetic_bounds
#endif

typedef enum { SOC_INTEGRAL_DIR_NONE, SOC_INTEGRAL_DIR_CHG, SOC_INTEGRAL_DIR_DSG } soc_integral_dir_t;
typedef struct {
    uint32_t rest_ms, ocv_correction_ms, full_ms, empty_ms;
    uint32_t minute_ms, minute_sum[SOC_MAX_CELLS];
    uint16_t minute_mv[SOC_MAX_CELLS][BMS_SOC_OCV_WINDOW_MINUTES];
    uint8_t minute_index, minute_count, rest_started, cell_count;
    uint8_t previous_full, previous_empty, full_consumed, empty_consumed, uvp_consumed;
    uint8_t soc_low_active[3], endpoint_state, ocv_high;
    uint16_t soc_low_count[3];
#if BMS_SOC_DIAG_STATE_ENABLE
    bms_soc_eta_t eta;
    uint16_t ocv_mv;
    uint8_t ocv_center, ocv_low, ocv_confidence, ocv_state, endpoint_event_flags;
    uint8_t last_sample_state, last_integral_direction, last_soc_action;
    uint8_t last_soc_before, last_soc_after, last_soc_target, last_decision_detail;
    uint32_t last_sample_elapsed_32k, last_integral_delta_as10;
#endif
} soc_runtime_t;

bms_soc_state_t g_bms_soc;
static soc_runtime_t g_soc_runtime;
/* 正余数为尚未扣除的放电量；反向电流自然抵消，不按方向清空。 */
static int32_t g_soc_integral_remainder;
static int32_t g_soc_input_current_ma;
static uint32_t g_soc_sample_ms, g_soc_strategy_pending_ms, g_soc_display_ms;
#if BMS_SOC_DIAG_STATE_ENABLE
static uint32_t g_soc_sample_tick_32k;
#endif
static uint8_t g_soc_input_valid, g_soc_initialized;
static uint8_t s_check_requested, s_check_seen_active;
static soc_integral_dir_t g_soc_last_active_dir;
static const bms_soc_profile_config_t *g_soc_profile;
static uint16_t s_persisted_generation;
static void SOC_Result_Pass(void);
static void soc_invalidate_sample_interval(void);
static uint8_t soc_limit_percent_u32(uint32_t v) { return v > 100u ? 100u : (uint8_t)v; }
static uint32_t soc_limit_cycle_u32(uint32_t v) { return v > SOC_CYCLE_MAX ? SOC_CYCLE_MAX : v; }
static uint8_t soc_limit_dsg_u32(uint32_t v) { return v > 99u ? 99u : (uint8_t)v; }
static uint32_t current_magnitude(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }
static uint32_t add_to_limit(uint32_t value, uint32_t elapsed, uint32_t limit)
{ return elapsed >= limit - value ? limit : value + elapsed; }

/* 记录 SOC 输入样本资格与时序诊断。 */
#if BMS_SOC_DIAG_STATE_ENABLE
static void soc_diag_note_sample(uint8_t state, soc_integral_dir_t dir,
                                 uint32_t elapsed_32k)
{
    uint8_t soc = get_soc_real();
    g_soc_runtime.last_sample_state = state;
    g_soc_runtime.last_integral_direction = (uint8_t)dir;
    g_soc_runtime.last_sample_elapsed_32k = elapsed_32k;
    g_soc_runtime.last_integral_delta_as10 = 0u;
    g_soc_runtime.last_soc_action = BMS_SOC_ACTION_NONE;
    g_soc_runtime.last_soc_before = soc;
    g_soc_runtime.last_soc_after = soc;
    g_soc_runtime.last_soc_target = soc;
    g_soc_runtime.last_decision_detail = 0u;
}

/* 记录 SOC 校正或锚定动作及原因。 */
static void soc_diag_note_action(uint8_t action, uint8_t before,
                                 uint8_t target, uint8_t detail)
{
    g_soc_runtime.last_soc_action = action;
    g_soc_runtime.last_soc_before = before;
    g_soc_runtime.last_soc_after = get_soc_real();
    g_soc_runtime.last_soc_target = target;
    g_soc_runtime.last_decision_detail = detail;
}
#else
#define soc_diag_note_sample(...) ((void)0)
#define soc_diag_note_action(...) ((void)0)
#endif

uint8_t bms_soh_from_cycle(uint16_t cycle)
{
    if (cycle <= 80u) return 100u;
    if (cycle >= 800u) return 80u;
    if (cycle <= 500u) {
        uint16_t x = (uint16_t)(cycle - 80u);
        uint16_t drop = (uint16_t)((uint32_t)x * 10u / 420u);
        return (uint8_t)(100u - drop);
    }
    {
        uint16_t x = (uint16_t)(cycle - 500u);
        uint16_t drop = (uint16_t)((uint32_t)x * 10u / 300u);
        return (uint8_t)(90u - drop);
    }
}


uint8_t get_soc_real(void) { return g_bms_soc.soc_estimate_percent; }

static uint32_t soc_nominal_capacity_0p1ah(void)
{
    bms_config_system_params_t system;
    if (bms_config_store_get_system(&system) && system.capacity_factory &&
        system.capacity_factory <= BMS_SOC_CAPACITY_MAX_0P1AH) return system.capacity_factory;
    return BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH;
}

static void soc_recalc_full_capacity(void)
{
    g_bms_soc.nominal_capacity_as10 = soc_nominal_capacity_0p1ah() * SOC_CAPACITY_UNITS_PER_FACTORY;
    g_bms_soc.soh = bms_soh_from_cycle((uint16_t)soc_limit_cycle_u32(g_bms_soc.cycle_count));
    g_bms_soc.effective_capacity_as10 =
        g_bms_soc.nominal_capacity_as10 * g_bms_soc.soh / 100u;
    if (!g_bms_soc.effective_capacity_as10) g_bms_soc.effective_capacity_as10 = 1u;
}

static void soc_recalc_now_capacity(void)
{
    g_bms_soc.remaining_capacity_as10 =
        (uint32_t)get_soc_real() * g_bms_soc.effective_capacity_as10 / 100u;
}

static void soc_update_percent(void)
{
    uint32_t full = g_bms_soc.effective_capacity_as10;
    g_bms_soc.soc_estimate_percent = full ?
        (uint8_t)(g_bms_soc.remaining_capacity_as10 * 100u / full) : 0u;
    /* 不从整数 SOC 判断 99% 资格边界。 */
    if (g_bms_soc.remaining_capacity_as10 * 100u < full * 99u) {
        g_bms_soc.full_confirmed = 0u;
        g_bms_soc.full_basis = 0u;
    }
}

/* 仅关联满电证据相关输入；普通 OCV 模型调整不凭空改写电量。 */
static uint32_t soc_full_basis(void)
{
    uint32_t hash = 2166136261u;
    uint32_t fields[8];
    uint8_t i, j;
    fields[0] = BMS_SOC_POLICY_VERSION;
    fields[1] = g_soc_profile->chemistry;
    fields[2] = soc_nominal_capacity_0p1ah();
    fields[3] = g_soc_profile->full_sync_mv;
    fields[4] = g_soc_profile->full_min_margin_mv;
    fields[5] = g_soc_profile->full_cell_delta_max_mv;
    fields[6] = BMS_SOC_FULL_HOLD_MS;
    fields[7] = BMS_PRODUCT_CELL_COUNT;
    for (i = 0u; i < 8u; ++i) for (j = 0u; j < 4u; ++j) {
        hash ^= (fields[i] >> (j * 8u)) & 255u;
        hash *= 16777619u;
    }
    return hash;
}

static void soc_reset_integral_accumulator(void) { g_soc_integral_remainder = 0; }

static void soc_reset_ocv_tracking(void)
{
    g_soc_runtime.rest_ms = 0u;
    g_soc_runtime.minute_ms = 0u;
    g_soc_runtime.minute_count = 0u;
    g_soc_runtime.minute_index = 0u;
    g_soc_runtime.rest_started = 0u;
    g_soc_runtime.ocv_correction_ms = 0u;
    memset(g_soc_runtime.minute_sum, 0, sizeof(g_soc_runtime.minute_sum));
#if BMS_SOC_DIAG_STATE_ENABLE
    g_soc_runtime.ocv_state = BMS_SOC_OCV_WAIT_CURRENT;
    g_soc_runtime.ocv_confidence = 0u;
#endif
}

static void soc_reset_voltage_confirmation(void)
{
    g_soc_runtime.full_ms = g_soc_runtime.empty_ms = 0u;
    g_soc_runtime.previous_full = g_soc_runtime.previous_empty = 0u;
    g_soc_runtime.full_consumed = g_soc_runtime.empty_consumed = 0u;
}

static void soc_invalidate_sample_interval(void)
{
    g_soc_input_valid = 0u;
    g_soc_last_active_dir = SOC_INTEGRAL_DIR_NONE;
    s_check_requested = s_check_seen_active = 0u;
    g_soc_strategy_pending_ms = 0u;
    soc_reset_ocv_tracking();
    soc_reset_voltage_confirmation();
    /* 暂时无样本不抹掉过去已确认的满电证据，也不补算盲区。 */
#if BMS_SOC_DIAG_STATE_ENABLE
    bms_soc_eta_reset(&g_soc_runtime.eta);
#endif
    memset(g_soc_runtime.soc_low_count, 0, sizeof(g_soc_runtime.soc_low_count));
}

static void soc_profile_refresh(void)
{
    const bms_soc_profile_config_t *profile = bms_config_current_soc_profile();
    uint16_t generation = bms_config_soc_profile_generation();
    if (!profile) profile = BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP ?
        &g_soc_profile_lfp : &g_soc_profile_nmc;
    if (g_soc_profile != profile || s_persisted_generation != generation) {
        g_soc_profile = profile;
        s_persisted_generation = generation;
        soc_reset_ocv_tracking();
        soc_reset_voltage_confirmation();
    }
}

/* 精确求floor(remaining*new_full/old_full)，不构造可能溢出的乘积。
 * remaining<=old_full<=23590800，逐位余数的临时值小于3*old_full。 */
static uint32_t soc_scale_remaining_capacity(uint32_t remaining, uint32_t old_full, uint32_t new_full)
{
    uint32_t quotient = 0u, remainder = 0u;
    uint8_t bit = 32u;
    if (remaining >= old_full) return new_full;
    while (bit) {
        uint32_t next;
        --bit;
        next = remainder * 2u + (((new_full >> bit) & 1u) ? remaining : 0u);
        quotient = quotient * 2u + next / old_full;
        remainder = next % old_full;
    }
    return quotient;
}

/* 只在容量基准变化时按 As*10 比例换算；模型重载保留电量及积分余数。 */
static void soc_resize_capacity(void)
{
    uint32_t before = g_bms_soc.effective_capacity_as10;
    soc_recalc_full_capacity();
    if (before && before != g_bms_soc.effective_capacity_as10) {
        g_bms_soc.remaining_capacity_as10 = soc_scale_remaining_capacity(
            g_bms_soc.remaining_capacity_as10, before, g_bms_soc.effective_capacity_as10);
        soc_reset_integral_accumulator();
    }
    soc_update_percent();
}

uint8_t bms_soc_reload_configuration(void)
{
    if (!bms_soc_profile_valid(bms_config_current_soc_profile())) return 0u;
    soc_profile_refresh();
    soc_invalidate_sample_interval();
    if (g_soc_initialized) {
        soc_resize_capacity();
        if (g_bms_soc.full_basis != soc_full_basis()) {
            g_bms_soc.full_confirmed = 0u;
            g_bms_soc.full_basis = 0u;
        }
        SOC_Result_Pass();
    }
    return 1u;
}

static soc_integral_dir_t soc_current_direction(uint16_t *magnitude_a10)
{
    uint32_t magnitude = current_magnitude(g_soc_input_current_ma);
    if (magnitude_a10) *magnitude_a10 = magnitude / 100u > 65535u ? 65535u : (uint16_t)(magnitude / 100u);
    /* 方向证据独立于积分，不能用噪声小电流授予端点。 */
    if (!g_soc_input_valid || magnitude <= BMS_CURRENT_UNRELIABLE_MAX_MA ||
        magnitude <= g_soc_profile->current_deadband_ma) return SOC_INTEGRAL_DIR_NONE;
    return g_soc_input_current_ma < 0 ? SOC_INTEGRAL_DIR_CHG : SOC_INTEGRAL_DIR_DSG;
}

static uint8_t soc_display_target(void)
{
    uint8_t target = get_soc_real();
    if (!g_bms_soc.full_confirmed && target == 100u) target = 99u;
    return target;
}

static void set_dispsoc(uint8_t soc)
{
    uint8_t target = soc_limit_percent_u32(soc);
    if (!g_bms_soc.full_confirmed && target == 100u) target = 99u;
    g_bms_report.soc.soc_percent = target;
    g_soc_display_ms = 0u;
}

static void soc_display_follow(uint32_t elapsed_ms)
{
    uint8_t target = soc_display_target();
    if (g_bms_report.soc.soc_percent == target) { g_soc_display_ms = 0u; return; }
    g_soc_display_ms = add_to_limit(g_soc_display_ms, elapsed_ms, BMS_SOC_DISPLAY_STEP_MS);
    if (g_soc_display_ms < BMS_SOC_DISPLAY_STEP_MS) return;
    g_soc_display_ms = 0u; /* 不预支、不攒步进，一次最多一个百分点。 */
    if (g_bms_report.soc.soc_percent < target) ++g_bms_report.soc.soc_percent;
    else --g_bms_report.soc.soc_percent;
}

static void SOC_Result_Pass(void)
{
    g_bms_report.soc.soh_percent = g_bms_soc.soh;
    g_bms_report.soc.cycle_count = (uint16_t)g_bms_soc.cycle_count;
    g_bms_report.soc.remaining_capacity_0p01ah = (uint16_t)(g_bms_soc.remaining_capacity_as10 / SOC_REPORT_CAPACITY_DIVISOR);
    g_bms_report.soc.effective_capacity_0p01ah = (uint16_t)(g_bms_soc.effective_capacity_as10 / SOC_REPORT_CAPACITY_DIVISOR);
    g_bms_report.soc.nominal_capacity_0p01ah = (uint16_t)(g_bms_soc.nominal_capacity_as10 / SOC_REPORT_CAPACITY_DIVISOR);
}

bms_state_store_data_t bms_soc_state_snapshot(void)
{
    bms_state_store_data_t state;
    state.soc = get_soc_real();
    state.dsg = g_bms_soc.discharge_fraction_percent;
    state.cycle = g_bms_soc.cycle_count;
    state.full_confirmed = g_bms_soc.full_confirmed;
    state.full_basis = g_bms_soc.full_confirmed ? g_bms_soc.full_basis : 0u;
    return state;
}

/* 只生成候选；State完整提交成功后，调用者才能发布人工设置。 */
bms_state_store_data_t bms_soc_manual_state_snapshot(uint8_t soc)
{
    bms_state_store_data_t state = bms_soc_state_snapshot();
    state.soc = soc_limit_percent_u32(soc);
    state.full_confirmed = state.soc == 100u;
    state.full_basis = state.full_confirmed ? soc_full_basis() : 0u;
    return state;
}

void set_soc_param(uint8_t soc)
{
    bms_state_store_data_t state = bms_soc_manual_state_snapshot(soc);
    g_bms_soc.soc_estimate_percent = (uint8_t)state.soc;
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
    g_bms_soc.full_confirmed = (uint8_t)state.full_confirmed;
    g_bms_soc.full_basis = state.full_basis;
    soc_reset_integral_accumulator();
    soc_invalidate_sample_interval();
    set_dispsoc(get_soc_real()); /* 人工服务操作立即发布，不等待运行期显示追赶。 */
    SOC_Result_Pass();
}

void bms_soc_nominal_capacity_changed(void)
{
    soc_resize_capacity();
    g_bms_soc.full_confirmed = 0u;
    g_bms_soc.full_basis = 0u;
    soc_invalidate_sample_interval();
    SOC_Result_Pass();
}

void bms_soc_cycle_count_changed(uint32_t cycle)
{
    g_bms_soc.cycle_count = soc_limit_cycle_u32(cycle);
    bms_soc_nominal_capacity_changed();
}

void soc_param_lib_init(const bms_state_store_data_t *state)
{
    bms_state_store_data_t defaults;
    memset(&g_soc_runtime, 0, sizeof(g_soc_runtime));
    memset(&g_bms_soc, 0, sizeof(g_bms_soc));
    if (!state) { defaults = bms_state_store_get_default_data(); state = &defaults; }
    soc_profile_refresh();
    g_bms_soc.cycle_count = soc_limit_cycle_u32(state->cycle);
    g_bms_soc.discharge_fraction_percent = soc_limit_dsg_u32(state->dsg);
    g_bms_soc.soc_estimate_percent = soc_limit_percent_u32(state->soc);
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
    g_bms_soc.full_basis = state->full_basis;
    g_bms_soc.full_confirmed = bms_config_current_soc_profile() != 0 && state->full_confirmed == 1u && state->soc >= 99u &&
        state->full_basis == soc_full_basis();
    if (!g_bms_soc.full_confirmed) g_bms_soc.full_basis = 0u;
    g_soc_last_active_dir = SOC_INTEGRAL_DIR_NONE;
    soc_reset_integral_accumulator();
    soc_invalidate_sample_interval();
    g_soc_initialized = 1u;
    set_dispsoc(get_soc_real()); /* 冷启动直接恢复显示；自动校准仍走限速追赶。 */
    SOC_Result_Pass();
}

static void soc_integrate(uint32_t elapsed_ms)
{
    /* 合格间隔<=400ms。先拆电流商/余数，使int32电流全范围也不溢出。 */
    int32_t fractional = (g_soc_input_current_ma % SOC_INTEGRAL_DENOMINATOR) * (int32_t)elapsed_ms +
                         g_soc_integral_remainder;
    int32_t delta = (g_soc_input_current_ma / SOC_INTEGRAL_DENOMINATOR) * (int32_t)elapsed_ms +
                    fractional / SOC_INTEGRAL_DENOMINATOR;
    uint8_t before = get_soc_real();
    uint32_t amount;
    g_soc_integral_remainder = fractional % SOC_INTEGRAL_DENOMINATOR;
    /* C除法向零截断；方向切换时必须把异号商/余数归一化。 */
    if (delta > 0 && g_soc_integral_remainder < 0) {
        --delta;
        g_soc_integral_remainder += SOC_INTEGRAL_DENOMINATOR;
    } else if (delta < 0 && g_soc_integral_remainder > 0) {
        ++delta;
        g_soc_integral_remainder -= SOC_INTEGRAL_DENOMINATOR;
    }
    if (delta > 0) {
        amount = (uint32_t)delta;
        if (amount >= g_bms_soc.remaining_capacity_as10) {
            g_bms_soc.remaining_capacity_as10 = 0u;
            if (g_soc_integral_remainder > 0) g_soc_integral_remainder = 0;
        } else g_bms_soc.remaining_capacity_as10 -= amount;
    } else if (delta < 0) {
        amount = (uint32_t)(-delta);
        if (amount >= g_bms_soc.effective_capacity_as10 - g_bms_soc.remaining_capacity_as10) {
            g_bms_soc.remaining_capacity_as10 = g_bms_soc.effective_capacity_as10;
            if (g_soc_integral_remainder < 0) g_soc_integral_remainder = 0;
        } else g_bms_soc.remaining_capacity_as10 += amount;
    }
    if ((g_bms_soc.remaining_capacity_as10 == 0u && g_soc_integral_remainder > 0) ||
        (g_bms_soc.remaining_capacity_as10 == g_bms_soc.effective_capacity_as10 && g_soc_integral_remainder < 0))
        soc_reset_integral_accumulator();
    soc_update_percent();
    /* 沿用等效放电百分比口径，只统计真实积分引起的下降。 */
    if (delta > 0 && before > get_soc_real()) {
        uint16_t discharged = g_bms_soc.discharge_fraction_percent + before - get_soc_real();
        if (discharged >= 100u) {
            discharged -= 100u;
            if (g_bms_soc.cycle_count < SOC_CYCLE_MAX) {
                ++g_bms_soc.cycle_count;
                soc_resize_capacity();
            }
        }
        g_bms_soc.discharge_fraction_percent = (uint8_t)discharged;
    }
    soc_diag_note_action(BMS_SOC_ACTION_INTEGRATE, before, get_soc_real(), 0u);
#if BMS_SOC_DIAG_STATE_ENABLE
    g_soc_runtime.last_integral_delta_as10 = delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
#endif
}

static uint8_t soc_cells_valid(const bms_soc_sample_t *sample, uint16_t *mean_mv)
{
    uint8_t i;
    uint16_t low = 65535u, high = 0u;
    uint32_t sum = 0u;
    if (!sample->voltage_valid || sample->afe_fault ||
        sample->cell_count != BMS_PRODUCT_CELL_COUNT || sample->cell_count > SOC_MAX_CELLS) return 0u;
    for (i = 0u; i < sample->cell_count; ++i) {
        uint16_t mv = sample->cell_mv[i];
        if (!mv || mv >= 60000u) return 0u;
        if (mv < low) low = mv;
        if (mv > high) high = mv;
        sum += mv;
    }
    if (low != sample->cell_min_mv || high != sample->cell_max_mv ||
        (uint16_t)(high - low) != sample->cell_delta_mv) return 0u;
    *mean_mv = (uint16_t)((sum + sample->cell_count / 2u) / sample->cell_count);
    return 1u;
}

uint8_t bms_soc_voltage_check_requested(void) { return s_check_requested; }

/* 每个端点动作/每一个 OCV 修正额度都消费一次新诊断，不能永久沿用开机健康。 */
static uint8_t soc_require_voltage_check(const bms_soc_sample_t *sample)
{
    if (s_check_requested && s_check_seen_active && sample->voltage_calibration_valid && !sample->voltage_check_active) {
        s_check_requested = s_check_seen_active = 0u;
        return 1u;
    }
    s_check_requested = 1u;
    return 0u;
}

static void soc_apply_endpoint(uint8_t full)
{
    uint8_t before = get_soc_real();
    (void)before;
    g_bms_soc.remaining_capacity_as10 = full ? g_bms_soc.effective_capacity_as10 : 0u;
    g_bms_soc.full_confirmed = full;
    g_bms_soc.full_basis = full ? soc_full_basis() : 0u;
    soc_reset_integral_accumulator();
    soc_update_percent();
    g_soc_runtime.endpoint_state = full ? BMS_SOC_ENDPOINT_CONFIRMED_FULL : BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
    soc_diag_note_action(full ? BMS_SOC_ACTION_FULL_ANCHOR : BMS_SOC_ACTION_FORCED_EMPTY,
                         before, full ? 100u : 0u, 0u);
}

static uint8_t soc_endpoints(const bms_soc_sample_t *sample, soc_integral_dir_t dir, uint32_t elapsed)
{
    uint8_t full, empty, uvp;
    uint16_t full_mv = g_soc_profile->full_sync_mv;
    full = sample->cell_max_mv >= full_mv &&
        sample->cell_min_mv >= full_mv - g_soc_profile->full_min_margin_mv &&
        sample->cell_delta_mv <= g_soc_profile->full_cell_delta_max_mv;
    uvp = sample->third_cell_uvp || sample->third_pack_uvp;
    if (!uvp) g_soc_runtime.uvp_consumed = 0u;
    if (full && sample->third_cell_ovp && !sample->current_fault &&
        (dir == SOC_INTEGRAL_DIR_CHG || (dir == SOC_INTEGRAL_DIR_NONE && g_soc_last_active_dir == SOC_INTEGRAL_DIR_CHG))) {
        if (!g_soc_runtime.full_consumed && !soc_require_voltage_check(sample)) return 1u;
        if (!g_soc_runtime.full_consumed) soc_apply_endpoint(1u);
        g_soc_runtime.full_consumed = 1u;
        return 1u;
    }
    if (uvp && !g_soc_runtime.uvp_consumed && !sample->current_fault && dir != SOC_INTEGRAL_DIR_CHG &&
        ((sample->third_cell_uvp && sample->cell_min_mv <= g_bms_protection_params.cell_uvp_third_mv) ||
         (sample->third_pack_uvp && sample->pack_voltage_10mv <= g_bms_protection_params.pack_uvp_third_10mv))) {
        if (!soc_require_voltage_check(sample)) return 1u;
        soc_apply_endpoint(0u);
        g_soc_runtime.uvp_consumed = 1u;
        return 1u;
    }
    full = full && dir == SOC_INTEGRAL_DIR_CHG && !sample->current_fault;
    empty = dir == SOC_INTEGRAL_DIR_DSG && !sample->current_fault &&
        sample->cell_min_mv <= g_soc_profile->empty_sync_mv &&
        sample->cell_delta_mv <= g_soc_profile->empty_max_margin_mv;
    if (!full) { g_soc_runtime.full_ms = 0u; g_soc_runtime.full_consumed = 0u; }
    else if (g_soc_runtime.previous_full) g_soc_runtime.full_ms = add_to_limit(g_soc_runtime.full_ms, elapsed, BMS_SOC_FULL_HOLD_MS);
    if (!empty) { g_soc_runtime.empty_ms = 0u; g_soc_runtime.empty_consumed = 0u; }
    else if (g_soc_runtime.previous_empty) g_soc_runtime.empty_ms = add_to_limit(g_soc_runtime.empty_ms, elapsed, BMS_SOC_EMPTY_HOLD_MS);
    g_soc_runtime.previous_full = full;
    g_soc_runtime.previous_empty = empty;
    if (full) {
        g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_FULL_APPROACH;
        if (g_soc_runtime.full_ms >= BMS_SOC_FULL_HOLD_MS) {
            if (!g_soc_runtime.full_consumed && !soc_require_voltage_check(sample)) return 1u;
            if (!g_soc_runtime.full_consumed) soc_apply_endpoint(1u);
            g_soc_runtime.full_consumed = 1u;
            return 1u;
        }
    }
    if (empty) {
        g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_EMPTY_APPROACH;
        if (g_soc_runtime.empty_ms >= BMS_SOC_EMPTY_HOLD_MS) {
            if (!g_soc_runtime.empty_consumed && !soc_require_voltage_check(sample)) return 1u;
            if (!g_soc_runtime.empty_consumed) soc_apply_endpoint(0u);
            g_soc_runtime.empty_consumed = 1u;
            return 1u;
        }
    }
    return 0u;
}

static uint8_t soc_ocv_upper(uint16_t mv)
{
    const bms_soc_ocv_config_point_t *points = g_soc_profile->points;
    uint16_t center = points[0].soc_percent;
    uint16_t band = g_soc_profile->ocv_error_band_percent;
    uint8_t i;
    if (mv >= points[g_soc_profile->point_count - 1u].mv) center = 100u;
    else if (mv > points[0].mv) for (i = 1u; i < g_soc_profile->point_count; ++i) {
        if (mv <= points[i].mv) {
            uint32_t width = points[i].mv - points[i - 1u].mv;
            /* 上界向上取整，避免整百分比截断变成额外下调。 */
            center = points[i - 1u].soc_percent +
                ((uint32_t)(mv - points[i - 1u].mv) * (points[i].soc_percent - points[i - 1u].soc_percent) + width - 1u) / width;
            break;
        }
    }
    if (band < g_soc_profile->ocv_min_error_band_percent) band = g_soc_profile->ocv_min_error_band_percent;
#if BMS_SOC_DIAG_STATE_ENABLE
    g_soc_runtime.ocv_mv = mv;
    g_soc_runtime.ocv_center = (uint8_t)center;
    g_soc_runtime.ocv_low = center > band ? (uint8_t)(center - band) : 0u;
#endif
    return center + band > 100u ? 100u : (uint8_t)(center + band);
}

static uint8_t soc_ocv_window(const bms_soc_sample_t *sample, uint32_t elapsed)
{
    uint8_t i, j;
    uint32_t portion;
    while (elapsed) { /* <=400ms，最多跨一个分钟边界。 */
        portion = SOC_MINUTE_MS - g_soc_runtime.minute_ms;
        if (portion > elapsed) portion = elapsed;
        for (i = 0u; i < sample->cell_count; ++i)
            g_soc_runtime.minute_sum[i] += (uint32_t)sample->cell_mv[i] * portion;
        g_soc_runtime.minute_ms += portion;
        elapsed -= portion;
        if (g_soc_runtime.minute_ms == SOC_MINUTE_MS) {
            for (i = 0u; i < sample->cell_count; ++i) {
                g_soc_runtime.minute_mv[i][g_soc_runtime.minute_index] =
                    (uint16_t)((g_soc_runtime.minute_sum[i] + SOC_MINUTE_MS / 2u) / SOC_MINUTE_MS);
                g_soc_runtime.minute_sum[i] = 0u;
            }
            g_soc_runtime.minute_ms = 0u;
            g_soc_runtime.minute_index = (uint8_t)((g_soc_runtime.minute_index + 1u) % BMS_SOC_OCV_WINDOW_MINUTES);
            if (g_soc_runtime.minute_count < BMS_SOC_OCV_WINDOW_MINUTES) ++g_soc_runtime.minute_count;
        }
    }
    if (g_soc_runtime.minute_count < BMS_SOC_OCV_WINDOW_MINUTES) return 0u;
    for (i = 0u; i < sample->cell_count; ++i) {
        uint16_t low = 65535u, high = 0u;
        for (j = 0u; j < BMS_SOC_OCV_WINDOW_MINUTES; ++j) {
            uint16_t value = g_soc_runtime.minute_mv[i][j];
            if (value < low) low = value;
            if (value > high) high = value;
        }
        if ((uint16_t)(high - low) > BMS_SOC_OCV_DRIFT_MAX_MV ||
            sample->cell_mv[i] + BMS_SOC_OCV_DRIFT_MAX_MV < low ||
            sample->cell_mv[i] > high + BMS_SOC_OCV_DRIFT_MAX_MV) return 0u;
    }
    return 1u;
}

static void soc_ocv(const bms_soc_sample_t *sample, uint16_t mean, uint32_t elapsed)
{
    uint8_t i, stable;
    uint32_t target, delta;
    if (current_magnitude(sample->current_ma) > BMS_SOC_OCV_CURRENT_MAX_MA ||
        sample->current_fault || sample->pack_fault || sample->balance_active ||
        sample->cell_delta_mv > BMS_SOC_OCV_DELTA_MAX_MV ||
        sample->cell_min_mv < g_soc_profile->valid_min_mv || sample->cell_max_mv > g_soc_profile->valid_max_mv) {
        soc_reset_ocv_tracking(); return;
    }
    if (g_soc_profile->chemistry == BMS_SOC_CHEMISTRY_LFP) for (i = 0u; i < sample->cell_count; ++i) {
        if (sample->cell_mv[i] >= BMS_SOC_LFP_PLATEAU_MIN_MV && sample->cell_mv[i] <= BMS_SOC_LFP_PLATEAU_MAX_MV) {
            soc_reset_ocv_tracking(); return;
        }
    }
    if (!g_soc_runtime.rest_started || g_soc_runtime.cell_count != sample->cell_count) {
        soc_reset_ocv_tracking();
        g_soc_runtime.rest_started = 1u;
        g_soc_runtime.cell_count = sample->cell_count;
        return;
    }
    stable = soc_ocv_window(sample, elapsed);
    g_soc_runtime.rest_ms = add_to_limit(g_soc_runtime.rest_ms, elapsed, BMS_SOC_OCV_REST_MS);
#if BMS_SOC_DIAG_STATE_ENABLE
    g_soc_runtime.ocv_state = BMS_SOC_OCV_PREPARE;
#endif
    if (g_soc_runtime.rest_ms < BMS_SOC_OCV_REST_MS || !stable) {
        g_soc_runtime.ocv_correction_ms = 0u; return;
    }
    g_soc_runtime.ocv_high = soc_ocv_upper(mean);
#if BMS_SOC_DIAG_STATE_ENABLE
    g_soc_runtime.ocv_state = BMS_SOC_OCV_READY;
    g_soc_runtime.ocv_confidence = 25u; /* 工程模型，非统计置信度。 */
#endif
    target = (g_bms_soc.effective_capacity_as10 * g_soc_runtime.ocv_high + 99u) / 100u;
    if (g_bms_soc.remaining_capacity_as10 <= target) {
        g_soc_runtime.ocv_correction_ms = 0u; return;
    }
    g_soc_runtime.ocv_correction_ms = add_to_limit(g_soc_runtime.ocv_correction_ms, elapsed, BMS_SOC_OCV_POINT_MS);
    if (g_soc_runtime.ocv_correction_ms < BMS_SOC_OCV_POINT_MS || !soc_require_voltage_check(sample)) return;
    /* 内部每次最多一个百分点，两个动作之间至少30分钟合格时间。
     * 向下取整守住上限；不在显示层掩盖内部电量跳变。 */
    delta = g_bms_soc.effective_capacity_as10 / 100u;
    if (delta > g_bms_soc.remaining_capacity_as10 - target) delta = g_bms_soc.remaining_capacity_as10 - target;
    g_soc_runtime.ocv_correction_ms = 0u;
    if (delta) {
        uint8_t before = get_soc_real();
        (void)before;
        g_bms_soc.remaining_capacity_as10 -= delta;
        soc_update_percent();
        soc_diag_note_action(BMS_SOC_ACTION_OCV_DOWN, before, g_soc_runtime.ocv_high, 0u);
    }
#if BMS_SOC_DIAG_STATE_ENABLE
    g_soc_runtime.ocv_state = BMS_SOC_OCV_CORRECT_DOWN;
#endif
}
/* 取得 SOC 运行诊断快照。 */
#if BMS_SERVICE_DIAG_ENABLE
void bms_soc_get_diag(bms_soc_diag_t *diag)
{
    uint32_t rest_s;
    if (diag == 0) return;
    memset(diag, 0, sizeof(*diag)); /* 原学习诊断槽保留，恒为零。 */
    if (g_soc_profile == 0) soc_profile_refresh();
    rest_s = g_soc_runtime.rest_ms / 1000u;
    if (rest_s > 65535u) rest_s = 65535u;

    diag->chemistry = g_soc_profile->chemistry;
    diag->profile_id = g_soc_profile->profile_id;
    diag->profile_version = g_soc_profile->profile_version;
    diag->soc_estimate = g_bms_soc.soc_estimate_percent;
    diag->soc_display = (uint8_t)g_bms_report.soc.soc_percent;
    diag->current_deadband_ma = g_soc_profile->current_deadband_ma;
    diag->ocv_state = g_soc_runtime.ocv_state;
    diag->ocv_center = g_soc_runtime.ocv_center;
    diag->ocv_low = g_soc_runtime.ocv_low;
    diag->ocv_high = g_soc_runtime.ocv_high;
    diag->ocv_confidence = g_soc_runtime.ocv_confidence;
    diag->ocv_cell_mv = g_soc_runtime.ocv_mv;
    diag->rest_seconds = (uint16_t)rest_s;
    diag->nominal_capacity_0p1ah = (uint16_t)(g_bms_soc.nominal_capacity_as10 /
                                               SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->effective_capacity_0p1ah = (uint16_t)(g_bms_soc.effective_capacity_as10 /
                                                 SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->remaining_capacity_0p1ah = (uint16_t)(g_bms_soc.remaining_capacity_as10 /
                                                 SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->endpoint_state = g_soc_runtime.endpoint_state;
    diag->endpoint_event_flags = g_soc_runtime.endpoint_event_flags;
    diag->filtered_current_ma = g_soc_runtime.eta.eta_filtered_current_ma;
    diag->current_variation_ma = (g_soc_runtime.eta.eta_variation_ma > 65535u) ?
        65535u : (uint16_t)g_soc_runtime.eta.eta_variation_ma;
    diag->time_to_empty_min = g_soc_runtime.eta.time_to_empty_min;
    diag->time_to_full_min = g_soc_runtime.eta.time_to_full_min;
    diag->eta_state = g_soc_runtime.eta.eta_state;
    diag->eta_direction = g_soc_runtime.eta.eta_direction;
    diag->eta_confidence = g_soc_runtime.eta.eta_confidence;
    diag->eta_valid = (g_soc_runtime.eta.eta_state == BMS_SOC_ETA_VALID);
    diag->soh = g_bms_soc.soh;
    diag->soh_source = BMS_SOC_SOH_SOURCE_ESTIMATED_CYCLE;
    diag->soh_confidence = 25u; /* 循环经验估算，不是实测容量。 */
    diag->last_sample_state = g_soc_runtime.last_sample_state;
    diag->last_integral_direction = g_soc_runtime.last_integral_direction;
    diag->last_soc_action = g_soc_runtime.last_soc_action;
    diag->last_soc_before = g_soc_runtime.last_soc_before;
    diag->last_soc_after = g_soc_runtime.last_soc_after;
    diag->last_soc_target = g_soc_runtime.last_soc_target;
    diag->last_decision_detail = g_soc_runtime.last_decision_detail;
    diag->last_sample_elapsed_32k = g_soc_runtime.last_sample_elapsed_32k;
    diag->last_integral_delta_as10 = g_soc_runtime.last_integral_delta_as10;
}
#endif

/* 用当前有效容量、电流和方向更新剩余时间。 */
#if BMS_SOC_DIAG_STATE_ENABLE
static void soc_eta_update(const bms_soc_sample_t *sample, soc_integral_dir_t dir)
{
    bms_soc_eta_input_t input;
    input.current_ma = g_soc_input_current_ma;
    input.remaining_as10 = g_bms_soc.remaining_capacity_as10;
    input.full_as10 = g_bms_soc.effective_capacity_as10;
    input.deadband_ma = g_soc_profile->current_deadband_ma;
    if (input.deadband_ma < BMS_CURRENT_UNRELIABLE_MAX_MA)
        input.deadband_ma = BMS_CURRENT_UNRELIABLE_MAX_MA;
    input.direction = dir == SOC_INTEGRAL_DIR_CHG ? BMS_SOC_ETA_DIR_CHARGE :
        (dir == SOC_INTEGRAL_DIR_DSG ? BMS_SOC_ETA_DIR_DISCHARGE : BMS_SOC_ETA_DIR_NONE);
    input.endpoint_active = g_soc_runtime.endpoint_state != BMS_SOC_ENDPOINT_NORMAL;
    input.near_full = sample->cell_max_mv + g_soc_profile->full_min_margin_mv >= g_soc_profile->full_sync_mv;
    bms_soc_eta_update(&g_soc_runtime.eta, &input);
}
#endif

/* 将低 SOC 保护延时转换为确认样本数。 */
static uint16_t soc_fault_filter_samples(void)
{
    uint32_t ms = (uint32_t)g_bms_protection_params.soc_low_filter_10ms * 10u;
    uint32_t samples = (ms + SOC_INTEGRAL_PERIOD_MS - 1u) / SOC_INTEGRAL_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

/* 取得指定级别的 SOC 故障位寄存器。 */
static bms_fault_reg_t *soc_fault_reg(uint8_t level)
{
    if (level == 0u) return &g_bms_report.fault_first;
    if (level == 1u) return &g_bms_report.fault_second;
    return &g_bms_report.fault_third;
}

/* 取得指定级别的低 SOC 阈值。 */
static uint16_t soc_fault_threshold(uint8_t level)
{
    if (level == 0u) return g_bms_protection_params.soc_low_first_percent;
    if (level == 1u) return g_bms_protection_params.soc_low_second_percent;
    return g_bms_protection_params.soc_low_third_percent;
}

/* 取得对应低 SOC 级别的历史故障编号。 */
static bms_fault_code_t soc_fault_history_code(uint8_t level)
{
    /* 低 SOC 故障编号同时用于故障记录。 */
    if (level == 0u) return BMS_FAULT_SOC_LOW_FIRST;
    if (level == 1u) return BMS_FAULT_SOC_LOW_SECOND;
    return BMS_FAULT_SOC_LOW_THIRD;
}

/* 更新低 SOC 故障触发、恢复与历史记录。 */
static void soc_update_low_faults(void)
{
    uint8_t level;
    uint8_t soc = get_soc_real();
    uint16_t needed = soc_fault_filter_samples();

    for (level = 0u; level < 3u; ++level) {
        uint16_t trip = soc_fault_threshold(level);
        uint16_t recover = g_bms_protection_params.soc_low_recover_percent;

        if (trip == 0u || trip > SOC_PERCENT_MAX) {
            g_soc_runtime.soc_low_active[level] = 0u;
            g_soc_runtime.soc_low_count[level] = 0u;
            soc_fault_reg(level)->bits.soc_low = 0u;
            continue;
        }
        if (recover <= trip) recover = (trip < SOC_PERCENT_MAX) ? (uint16_t)(trip + 1u) : SOC_PERCENT_MAX;

        /* SOC 告警的触发和恢复都要求连续样本，当前活动状态选择对应阈值。 */
        if (g_soc_runtime.soc_low_active[level] ? soc >= recover : soc <= trip) {
            if (g_soc_runtime.soc_low_count[level] < needed)
                g_soc_runtime.soc_low_count[level]++;
            if (g_soc_runtime.soc_low_count[level] >= needed) {
                g_soc_runtime.soc_low_active[level] = !g_soc_runtime.soc_low_active[level];
                g_soc_runtime.soc_low_count[level] = 0u;
                if (g_soc_runtime.soc_low_active[level])
                    bms_fault_history_record(soc_fault_history_code(level));
            }
        } else {
            g_soc_runtime.soc_low_count[level] = 0u;
        }
        soc_fault_reg(level)->bits.soc_low = g_soc_runtime.soc_low_active[level];
    }
}


void bms_soc_process_sample(const bms_soc_sample_t *sample)
{
    uint32_t elapsed, voltage_elapsed;
#if BMS_SOC_DIAG_STATE_ENABLE
    uint32_t elapsed_32k = 0u;
#endif
    uint16_t mean = 0u;
    uint8_t voltage_ready;
    soc_integral_dir_t dir;
    if (!sample || !g_soc_initialized || !sample->sample_valid) {
        soc_diag_note_sample(BMS_SOC_SAMPLE_INVALID, SOC_INTEGRAL_DIR_NONE, 0u);
        soc_invalidate_sample_interval(); return;
    }
    soc_profile_refresh();
    if (!g_soc_input_valid) {
        g_soc_sample_ms = sample->timestamp_ms;
#if BMS_SOC_DIAG_STATE_ENABLE
        g_soc_sample_tick_32k = sample->timestamp_32k;
#endif
        g_soc_input_current_ma = sample->current_ma;
        g_soc_input_valid = 1u;
        soc_diag_note_sample(BMS_SOC_SAMPLE_FIRST, SOC_INTEGRAL_DIR_NONE, 0u);
        return;
    }
    elapsed = sample->timestamp_ms - g_soc_sample_ms;
    if (!elapsed) { soc_diag_note_sample(BMS_SOC_SAMPLE_DUPLICATE, soc_current_direction(0), 0u); return; }
    g_soc_sample_ms = sample->timestamp_ms;
#if BMS_SOC_DIAG_STATE_ENABLE
    elapsed_32k = sample->timestamp_32k - g_soc_sample_tick_32k;
    g_soc_sample_tick_32k = sample->timestamp_32k;
#endif
    g_soc_input_current_ma = sample->current_ma;
    if (elapsed > BMS_SOC_MAX_SAMPLE_GAP_MS) {
        soc_invalidate_sample_interval();
        g_soc_input_valid = 1u; /* 当前有效帧成为新的起点。 */
        soc_diag_note_sample(BMS_SOC_SAMPLE_GAP, SOC_INTEGRAL_DIR_NONE, elapsed_32k);
        return;
    }
    dir = soc_current_direction(0);
    soc_diag_note_sample(BMS_SOC_SAMPLE_ACCEPTED, dir, elapsed_32k);
    soc_integrate(elapsed); /* 不使用电压、温度或方向死区屏蔽有效电流。 */
    voltage_ready = soc_cells_valid(sample, &mean) && g_soc_profile->profile_version == BMS_SOC_POLICY_VERSION;
    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_NORMAL;
    voltage_elapsed = s_check_requested && s_check_seen_active ? 0u : elapsed;
    if (sample->voltage_check_active) {
        if (s_check_requested) s_check_seen_active = 1u;
        else { soc_reset_voltage_confirmation(); soc_reset_ocv_tracking(); }
        /* 有界主动激励不是电池静置证据：暂停电压时钟，不把激励值放入窗口。
         * 电流仍已积分；实际电流活动会撤销静置资格。 */
        if (current_magnitude(sample->current_ma) > BMS_SOC_OCV_CURRENT_MAX_MA) soc_reset_ocv_tracking();
    } else if (!voltage_ready) {
        soc_reset_voltage_confirmation();
        soc_reset_ocv_tracking();
    } else if (soc_endpoints(sample, dir, voltage_elapsed)) soc_reset_ocv_tracking();
    else soc_ocv(sample, mean, voltage_elapsed);
    if (s_check_requested && s_check_seen_active && !sample->voltage_check_active) {
        /* 诊断失败或候选条件已消失，都不能把这次结果留给未来的另一事件。 */
        s_check_requested = s_check_seen_active = 0u;
    }
    if (dir != SOC_INTEGRAL_DIR_NONE) g_soc_last_active_dir = dir;
    g_soc_strategy_pending_ms += elapsed;
    while (g_soc_strategy_pending_ms >= SOC_INTEGRAL_PERIOD_MS) {
        g_soc_strategy_pending_ms -= SOC_INTEGRAL_PERIOD_MS;
        soc_update_low_faults();
#if BMS_SOC_DIAG_STATE_ENABLE
        soc_eta_update(sample, dir);
#endif
    }
    soc_display_follow(elapsed);
    SOC_Result_Pass();
}

#if BMS_SOC_BOARD_TEST_ENABLE
/* 使用实板 AFE/调度时间；仅在 SOC 入口替换物理量，AFE 保护与 MOS 不使用这些值。
 * 每段约 20 秒，结果保留 RAM，避免 PC 轮询漏掉 200 ms 样本。 */
static const int32_t s_board_currents_ma[] = {
    0, 199, -199, 200, -200, 201, -201, 3000, -3000, 20000, -20000, -3000,
    -3000, -3000
};
#define SOC_BOARD_CASE_COUNT 14u
#define SOC_BOARD_CASE_TICKS (20u * 32000u)
static uint16_t s_board_words[SOC_BOARD_CASE_COUNT][20];
static bms_soc_state_t s_board_saved_soc;
static uint32_t s_board_started_tick;
static uint32_t s_board_suspend_count, s_board_suspend_start;
static uint8_t s_board_started, s_board_case, s_board_saved_display;

static void board_put32(uint16_t *words, uint32_t value)
{
    words[0]=(uint16_t)value; words[1]=(uint16_t)(value>>16);
}

static uint32_t board_get32(const uint16_t *words)
{
    return (uint32_t)words[0] | ((uint32_t)words[1]<<16);
}

uint8_t bms_soc_board_test_active(void)
{
    return s_board_started && s_board_case < SOC_BOARD_CASE_COUNT;
}

void bms_soc_board_test_suspend_exits(uint32_t count)
{
    s_board_suspend_count=count;
}

uint8_t bms_soc_board_test_keep_awake(void)
{
    return bms_soc_board_test_active() && s_board_case != 0u && s_board_case != 11u;
}

void bms_soc_board_test_prepare(bms_soc_sample_t *sample, uint32_t observation_tick_32k)
{
    uint16_t *record;
    uint8_t new_case=0u;
    if (!s_board_started) {
        if (!sample->sample_valid || !sample->voltage_valid) return;
        s_board_saved_soc=g_bms_soc;
        s_board_saved_display=(uint8_t)g_bms_report.soc.soc_percent;
        s_board_started=1u;
        new_case=1u;
    } else if (s_board_case < SOC_BOARD_CASE_COUNT &&
               (uint32_t)(observation_tick_32k-s_board_started_tick) >= SOC_BOARD_CASE_TICKS) {
        s_board_words[s_board_case][0] |= 1u;
        ++s_board_case;
        new_case=1u;
    }
    if (!bms_soc_board_test_active()) {
        if (new_case) {
            g_bms_soc=s_board_saved_soc;
            soc_invalidate_sample_interval();
            soc_reset_integral_accumulator();
            set_dispsoc(s_board_saved_display);
            SOC_Result_Pass();
        }
        return;
    }
    record=s_board_words[s_board_case];
    if (new_case) {
        s_board_started_tick=observation_tick_32k;
        s_board_suspend_start=s_board_suspend_count;
        set_soc_param(50u);
        record[0]=bms_soc_board_test_keep_awake() ? 2u : 0u;
        board_put32(&record[2],(uint32_t)s_board_currents_ma[s_board_case]);
        board_put32(&record[4],sample->timestamp_32k);
        board_put32(&record[8],g_bms_soc.remaining_capacity_as10);
    }
    sample->current_ma=s_board_currents_ma[s_board_case];
    /* 中段电压与清除 SOC 锚点输入只用于测纯积分，真实报告/硬件保护不变。 */
    sample->cell_min_mv=3300u; sample->cell_max_mv=3310u; sample->cell_delta_mv=10u;
    sample->third_cell_ovp=0u; sample->third_cell_uvp=0u; sample->third_pack_uvp=0u;
    if (s_board_case == 12u || s_board_case == 13u) {
        /* 在 SOC 输入中验证单串过压不等于整包满电，不修改真实保护报告。 */
        sample->third_cell_ovp=1u;
        sample->cell_min_mv=(s_board_case == 12u) ? 3300u : 3500u;
        sample->cell_max_mv=(s_board_case == 12u) ? 3700u : 3510u;
        sample->cell_delta_mv=sample->cell_max_mv-sample->cell_min_mv;
    }
    sample->cell_count = BMS_PRODUCT_CELL_COUNT;
    for (uint8_t i = 0u; i < sample->cell_count; ++i) sample->cell_mv[i] = sample->cell_min_mv;
    sample->cell_mv[sample->cell_count - 1u] = sample->cell_max_mv;

}

void bms_soc_board_test_note(void)
{
    uint16_t *record;
    uint8_t state;
    if (!bms_soc_board_test_active()) return;
    record=s_board_words[s_board_case];
    if (record[1] != 0xFFFFu) ++record[1];
    board_put32(&record[6],g_soc_sample_tick_32k);
    board_put32(&record[10],g_bms_soc.remaining_capacity_as10);
    state=g_soc_runtime.last_sample_state;
    if (state==BMS_SOC_SAMPLE_ACCEPTED || state==BMS_SOC_SAMPLE_DIRECTION_CHANGE) ++record[12];
    if (state==BMS_SOC_SAMPLE_GAP) {
        ++record[13];
        board_put32(&record[16],board_get32(&record[16])+g_soc_runtime.last_sample_elapsed_32k);
    }
    if (state==BMS_SOC_SAMPLE_DUPLICATE) ++record[14];
    if (state==BMS_SOC_SAMPLE_INVALID) ++record[15];
    board_put32(&record[18],s_board_suspend_count-s_board_suspend_start);
}

uint16_t bms_soc_board_test_word(uint16_t offset)
{
    if (offset>=16u+SOC_BOARD_CASE_COUNT*20u) return 0u;
    if (offset>=16u) return s_board_words[(offset-16u)/20u][(offset-16u)%20u];
    switch (offset) {
    case 0u:return 0x5342u;
    case 1u:return 1u;
    case 2u:return s_board_case;
    case 3u:return SOC_BOARD_CASE_COUNT;
    case 4u:return 20u;
    case 5u:return 20u;
    case 6u:return bms_soc_board_test_active();
    case 7u:return bms_soc_board_test_keep_awake();
    case 8u:return (uint16_t)BMS_DIAG_BUILD_ID;
    case 9u:return (uint16_t)(BMS_DIAG_BUILD_ID>>16);
    case 10u:return s_board_saved_soc.soc_estimate_percent;
    default:return 0u;
    }
}
#endif
