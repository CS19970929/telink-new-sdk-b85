/*
 * 文件功能：SOC 积分、OCV 校正、端点约束与循环 SOH；明确有效样本、时间差和持久状态之
 * 间的边界。
 * bms/core/bms_soc.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_soc.h"
#include "bms_product.h"
/* 把 SOC、容量、循环和 SOH 结果发布到公共报告。 */
static void SOC_Result_Pass(void);

#include "bms_config_store.h"
#include "bms_soc_profile.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_parameters.h"
#include "bms_diag.h"
#include <string.h>

/* 仅开发配置在既有 Trace 环形缓存捕获 COW 恢复输入；默认构建不产生这些私有调试标记。 */
#ifndef BMS_SOC_OCV_TRACE_ENABLE
#define BMS_SOC_OCV_TRACE_ENABLE 0
#endif
#define SOC_TRACE_OCV_RESET              0x8000u
#define SOC_TRACE_OCV_COW_INPUT          0x8010u
#define SOC_TRACE_OCV_SLOPE_RESET        0x8020u

#define VCELLMAX g_soc_input.cell_max_mv
#define VCELLMIN g_soc_input.cell_min_mv
#define ICHG     g_stCellInfoReport.u16Ichg
#define IDSG     g_stCellInfoReport.u16IDischg

#define SOC_INTEGRAL_PERIOD_MS              200u
#define SOC_INTEGRAL_MS_PER_SEC             1000u
#define SOC_TICKS_PER_SECOND                (SOC_INTEGRAL_MS_PER_SEC / SOC_INTEGRAL_PERIOD_MS)
#define SOC_CAPACITY_UNITS_PER_AH           (3600u * 10u)
#define SOC_CAPACITY_FACTORY_UNITS_PER_AH   10u
#define SOC_CAPACITY_UNITS_PER_FACTORY      (SOC_CAPACITY_UNITS_PER_AH / SOC_CAPACITY_FACTORY_UNITS_PER_AH)
#define SOC_REPORT_CAPACITY_DIVISOR         360u
#define SOC_PERCENT_MAX                     100u
#define SOC_EQUIV_CYCLE_PERCENT             100u
#define SOC_DSG_INT_MAX                     (SOC_EQUIV_CYCLE_PERCENT - 1u)
#define SOC_CYCLE_MAX                       65535u

#define SOC_CURRENT_DEADBAND_MA_DEFAULT     200u
#define SOC_OCV_REST_PREPARE_SECONDS        600u
#define SOC_OCV_ERROR_BAND_PERCENT          5u
#define SOC_OCV_IDLE_SLOPE_MAX_MV           8u
#define SOC_OCV_IDLE_CELL_DELTA_MAX_MV      100u
#define SOC_OCV_OPENWIRE_CONFIRM_SAMPLES   3u
/* 一秒 COW 窗口、最后一帧诊断和三帧正常样本；每个接受帧仍独立受 400 ms 门禁限制。 */
#define SOC_OCV_OPENWIRE_MAX_PAUSE_32K \
    (BMS_SOC_TIME_TICKS_PER_SECOND + \
     (SOC_OCV_OPENWIRE_CONFIRM_SAMPLES + 1u) * BMS_SOC_MAX_SAMPLE_GAP_32K)
#define SOC_OCV_CELL_DELTA_MAX_MV           200u
#define SOC_LONG_REST_DOWN_STEP_TICKS       (SOC_TICKS_PER_SECOND * 60u * 30u)
#define SOC_FULL_LOCK_TICKS                 (SOC_TICKS_PER_SECOND * 60u)
#define SOC_FULL_SYNC_STEP_TICKS            (SOC_TICKS_PER_SECOND * 2u)
#define SOC_EMPTY_LOCK_TICKS                (SOC_TICKS_PER_SECOND * 5u)
#define SOC_EMPTY_SYNC_STEP_TICKS           SOC_TICKS_PER_SECOND
#define SOC_DISPLAY_STEP_TICKS              SOC_TICKS_PER_SECOND
#define SOC_DISPLAY_APPROACH_STEP_TICKS     4u
#define SOC_DISPLAY_CONFIRMED_FULL_TICKS    2u
#define SOC_DSG_EMPTY_LOCK_TICKS            (SOC_TICKS_PER_SECOND * 2u)
#define SOC_DSG_SAG_HOLD_CURR_MIN_A10       50u
#define SOC_DSG_SAG_HOLDOFF_TICKS           (SOC_TICKS_PER_SECOND * 60u)
#define SOC_DSG_SAG_HOLDOFF_HIGH_TICKS      (SOC_TICKS_PER_SECOND * 90u)
#define SOC_DSG_CURRENT_MIN_A10              2u
#define SOC_DSG_NATURAL_STEP_MIN_TICKS      SOC_TICKS_PER_SECOND
#define SOC_DSG_NATURAL_STEP_MAX_TICKS      (SOC_TICKS_PER_SECOND * 60u * 10u)
#define SOC_DSG_CORR_STEP_MIN_TICKS          (SOC_TICKS_PER_SECOND * 10u)
#define SOC_DSG_CORR_STEP_MAX_TICKS          (SOC_TICKS_PER_SECOND * 180u)
#define SOC_DSG_CORR_MID_GAP_PERCENT         6u
#define SOC_DSG_CORR_LARGE_GAP_PERCENT       10u
#define SOC_DSG_CORR_SMALL_GAP_MUL           4u
#define SOC_DSG_CORR_MID_GAP_MUL             3u
#define SOC_DSG_CORR_LARGE_GAP_MUL           2u
#define SOC_AUTO_LFP_OVP_MAX_MV              3900u
#define SOC_ENDPOINT_EVENT_EARLY_UVP         0x01u
#define SOC_ENDPOINT_EVENT_LARGE_SAG         0x02u
#define SOC_ENDPOINT_EVENT_IMBALANCE         0x04u
#define SOC_ENDPOINT_EVENT_CAPACITY_MISMATCH 0x08u
#define SOC_OCV_TEMP_MIN_X10                 200u  /* 温度 -20 ℃。 */
#define SOC_OCV_TEMP_MAX_X10                 1000u /* 温度 +60 ℃。 */


typedef enum
{
    SOC_INTEGRAL_DIR_NONE = 0,
    SOC_INTEGRAL_DIR_CHG,
    SOC_INTEGRAL_DIR_DSG,
} soc_integral_dir_t;

typedef enum
{
    SOC_OCV_OPENWIRE_NONE = 0,
    SOC_OCV_OPENWIRE_PAUSED,
    SOC_OCV_OPENWIRE_RECOVERING,
} soc_ocv_openwire_phase_t;

typedef struct
{
    uint32_t idle_stable_ticks;
    uint32_t ocv_down_ticks;
    uint32_t ocv_openwire_started_32k;
    uint32_t ocv_openwire_confirm_tick_32k;
    soc_ocv_openwire_phase_t ocv_openwire_phase;
    uint8_t ocv_openwire_confirm_samples;
    uint16_t idle_ocv_last_mv;
    uint16_t ocv_mv;
    uint8_t idle_ocv_mv_valid;
    uint8_t ocv_center;
    uint8_t ocv_low;
    uint8_t ocv_high;
    uint8_t ocv_confidence;
    uint8_t ocv_state;

    uint16_t full_lock_ticks;
    uint16_t full_adjust_ticks;
    uint8_t full_anchor_latched;
    uint16_t empty_lock_ticks;
    uint16_t empty_adjust_ticks;
    uint8_t empty_anchor_latched;

    uint16_t dsg_terminal_adjust_ticks;
    uint16_t dsg_empty_lock_ticks;
    uint16_t dsg_sag_hold_ticks;

    uint16_t soc_low_trip_count[3];
    uint16_t soc_low_recover_count[3];
    uint8_t soc_low_active[3];

    bms_soc_eta_t eta;

    uint8_t endpoint_state;
    uint8_t endpoint_event_flags;

    uint8_t last_sample_state;
    uint8_t last_integral_direction;
    uint8_t last_soc_action;
    uint8_t last_soc_before;
    uint8_t last_soc_after;
    uint8_t last_soc_target;
    uint8_t last_decision_detail;
    uint32_t last_sample_elapsed_32k;
    uint32_t last_integral_delta_as10;
} soc_runtime_t;

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;
static soc_runtime_t g_soc_runtime;
static soc_integral_dir_t g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
/* mA 乘 32K tick 的余数；每 As*10 单位分母含 100 mA。 */
static uint32_t g_soc_integral_tick_remainder;
static int32_t g_soc_input_current_ma;
/* 当前 SOC 输入快照的所有者；有效标志、时间戳和电流必须来自同一采样周期。 */
static bms_soc_sample_t g_soc_input;
static uint32_t g_soc_sample_tick_32k;
static uint32_t g_soc_interval_32k;
static uint32_t g_soc_strategy_pending_32k;
static uint8_t g_soc_input_valid;
static uint8_t g_soc_charger_state_ready;
static uint8_t g_soc_last_charger_present;
static uint8_t g_soc_load_state_ready;
static uint8_t g_soc_last_load_present;
static uint8_t g_soc_display_soc = (uint8_t)BMS_STATE_DEFAULT_SOC;
static uint8_t g_soc_display_step_ticks;
static uint8_t g_soc_initialized;
static bms_soc_config_t g_soc_config = {
    BMS_SOC_CHEMISTRY_AUTO,
    BMS_SOC_PROFILE_AUTO,
    SOC_CURRENT_DEADBAND_MA_DEFAULT,
    SOC_OCV_REST_PREPARE_SECONDS,
    SOC_OCV_ERROR_BAND_PERCENT,
};
static const soc_profile_t *g_soc_profile;

/* 按名义容量和循环 SOH 重算满容量。 */
static void soc_recalc_full_capacity(void);
/* 按当前 SOC 重算剩余容量。 */
static void soc_recalc_now_capacity(void);
/* 复位静置、斜率和断线恢复跟踪状态。 */
static void soc_reset_ocv_tracking(void);
/* 刷新当前化学体系和 OCV 策略参数。 */
static void soc_profile_refresh(void);
/* 样本失效时撤销积分时间区间并复位相关跟踪。 */
static void soc_invalidate_sample_interval(void);

/* 记录 SOC 输入样本资格与时序诊断。 */
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

/* 根据循环次数估算 SOH。 */
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

/* 构造 SOC 算法默认值与自动化学体系设置。 */
void bms_soc_get_default_config(bms_soc_config_t *config)
{
    if (config == 0) return;
    config->chemistry = BMS_SOC_CHEMISTRY_AUTO;
    config->profile_id = BMS_SOC_PROFILE_AUTO;
    config->current_deadband_ma = SOC_CURRENT_DEADBAND_MA_DEFAULT;
    config->ocv_rest_prepare_s = SOC_OCV_REST_PREPARE_SECONDS;
    config->ocv_error_band_percent = SOC_OCV_ERROR_BAND_PERCENT;
}

/* 检查 SOC 产品输入是否完整有效。 */
static uint8_t soc_product_config_valid(uint8_t chemistry, uint8_t profile_id)
{
    if ((chemistry > BMS_SOC_CHEMISTRY_NMC) ||
        (profile_id > BMS_SOC_PROFILE_GENERIC_NMC)) return 0u;
    if ((chemistry == BMS_SOC_CHEMISTRY_LFP) &&
        (profile_id == BMS_SOC_PROFILE_GENERIC_NMC)) return 0u;
    if ((chemistry == BMS_SOC_CHEMISTRY_NMC) &&
        (profile_id == BMS_SOC_PROFILE_GENERIC_LFP)) return 0u;
    return 1u;
}

/* 校验 SOC 配置范围与化学体系标识。 */
uint8_t bms_soc_config_valid(const bms_soc_config_t *config)
{
    if (config == 0) return 0u;
    if (!soc_product_config_valid(config->chemistry, config->profile_id)) return 0u;
    if ((config->current_deadband_ma > 2000u) ||
        (config->ocv_rest_prepare_s < 60u) ||
        (config->ocv_rest_prepare_s > 3600u) ||
        (config->ocv_error_band_percent == 0u) ||
        (config->ocv_error_band_percent > 20u)) return 0u;
    return 1u;
}

/* 应用有效 SOC 配置并刷新曲线与容量状态。 */
uint8_t bms_soc_configure(const bms_soc_config_t *config)
{
    if (!bms_soc_config_valid(config)) return 0u;

    if (!bms_config_store_set_soc(config)) return 0u;
    g_soc_config = *config;
    soc_invalidate_sample_interval();
    soc_profile_refresh();
    soc_reset_ocv_tracking();
    if (g_soc_initialized) {
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }
    return 1u;
}

/* 按化学体系 ID 查找 OCV 曲线配置。 */
static const soc_profile_t *soc_profile_from_id(uint8_t profile_id)
{
    if (profile_id == BMS_SOC_PROFILE_GENERIC_LFP) return &g_soc_profile_lfp;
    if (profile_id == BMS_SOC_PROFILE_GENERIC_NMC) return &g_soc_profile_nmc;
    return 0;
}

/* 根据配置及产品条件确定使用的化学体系。 */
static uint8_t soc_resolve_chemistry(void)
{
    const soc_profile_t *selected;
    uint16_t ovp;

    selected = soc_profile_from_id(g_soc_config.profile_id);
    if (selected != 0) return selected->chemistry;

    if (g_soc_config.chemistry == BMS_SOC_CHEMISTRY_LFP ||
        g_soc_config.chemistry == BMS_SOC_CHEMISTRY_NMC) {
        return g_soc_config.chemistry;
    }

    /* AUTO 是明确的通用选择；D008 默认使用编译装配身份，不迁移旧 Flash。 */
    ovp = g_bms_protection_params.u16VcellOvp_Third;
    if ((ovp >= 3300u) && (ovp <= SOC_AUTO_LFP_OVP_MAX_MV)) return BMS_SOC_CHEMISTRY_LFP;
    if ((ovp > SOC_AUTO_LFP_OVP_MAX_MV) && (ovp <= 4500u)) return BMS_SOC_CHEMISTRY_NMC;

    return BMS_SOC_CHEMISTRY_NMC;
}

/* 刷新当前化学体系和 OCV 策略参数。 */
static void soc_profile_refresh(void)
{
    const soc_profile_t *next = soc_profile_from_id(g_soc_config.profile_id);
    if (next == 0) {
        uint8_t chemistry = soc_resolve_chemistry();
        next = (chemistry == BMS_SOC_CHEMISTRY_LFP) ?
            &g_soc_profile_lfp : &g_soc_profile_nmc;
    }
    if (g_soc_profile != next) {
        g_soc_profile = next;
        soc_reset_ocv_tracking();
    }
}

/* 加载持久化 SOC 配置并检查产品适配。 */
static void soc_load_persisted_product_config(void)
{
    (void)bms_config_store_get_soc(&g_soc_config);
}

/* 取得 SOC 运行诊断快照。 */
void bms_soc_get_diag(bms_soc_diag_t *diag)
{
    uint32_t rest_s;
    if (diag == 0) return;
    memset(diag, 0, sizeof(*diag)); /* 原学习诊断槽保留，恒为零。 */
    if (g_soc_profile == 0) soc_profile_refresh();
    rest_s = g_soc_runtime.idle_stable_ticks / SOC_TICKS_PER_SECOND;
    if (rest_s > 65535u) rest_s = 65535u;

    diag->chemistry = g_soc_profile->chemistry;
    diag->profile_id = g_soc_profile->profile_id;
    diag->profile_version = g_soc_profile->profile_version;
    diag->soc_estimate = SOC_Calculate_Element.u8SOC_Now;
    diag->soc_display = g_soc_display_soc;
    diag->current_deadband_ma = g_soc_config.current_deadband_ma;
    diag->ocv_state = g_soc_runtime.ocv_state;
    diag->ocv_center = g_soc_runtime.ocv_center;
    diag->ocv_low = g_soc_runtime.ocv_low;
    diag->ocv_high = g_soc_runtime.ocv_high;
    diag->ocv_confidence = g_soc_runtime.ocv_confidence;
    diag->ocv_cell_mv = g_soc_runtime.ocv_mv;
    diag->rest_seconds = (uint16_t)rest_s;
    diag->nominal_capacity_0p1ah = (uint16_t)(SOC_Calculate_Element.u32CapFactory /
                                               SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->effective_capacity_0p1ah = (uint16_t)(SOC_Calculate_Element.u32CapFull /
                                                 SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->remaining_capacity_0p1ah = (uint16_t)(SOC_Calculate_Element.u32CapNow /
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
    diag->eta_valid = g_soc_runtime.eta.eta_valid;
    diag->soh = SOC_Calculate_Element.soh;
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

/* 将百分比限制到合法范围。 */
static uint8_t soc_limit_percent_u32(uint32_t value)
{
    return (value > SOC_PERCENT_MAX) ? SOC_PERCENT_MAX : (uint8_t)value;
}

/* 限制累计放电百分比余量。 */
static uint8_t soc_limit_dsg_u32(uint32_t value)
{
    return (value > SOC_DSG_INT_MAX) ? SOC_DSG_INT_MAX : (uint8_t)value;
}

/* 限制循环计数以避免字段溢出。 */
static uint32_t soc_limit_cycle_u32(uint32_t value)
{
    return (value > SOC_CYCLE_MAX) ? SOC_CYCLE_MAX : value;
}

/* 将循环次数安全转换为 16 位报告值。 */
static uint16_t soc_cycle_to_u16(uint32_t value)
{
    return (uint16_t)soc_limit_cycle_u32(value);
}

/* 计算两个 16 位值的无符号绝对差。 */
static uint16_t soc_abs_diff_u16(uint16_t a, uint16_t b)
{
    return (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

/* 结合电流死区判断当前充放电方向。 */
static soc_integral_dir_t soc_current_direction(uint16_t *magnitude_a10)
{
    uint32_t magnitude_ma;
    soc_integral_dir_t dir = SOC_INTEGRAL_DIR_NONE;

    if (magnitude_a10 != 0) *magnitude_a10 = 0u;
    if (!g_soc_input_valid) return dir;
    /* 无符号减法处理 INT32_MIN，避免有符号溢出。 */
    magnitude_ma = (g_soc_input_current_ma < 0) ?
        (0u - (uint32_t)g_soc_input_current_ma) : (uint32_t)g_soc_input_current_ma;
    if (magnitude_ma <= BMS_CURRENT_UNRELIABLE_MAX_MA ||
        magnitude_ma <= g_soc_config.current_deadband_ma) return dir;
    dir = (g_soc_input_current_ma < 0) ? SOC_INTEGRAL_DIR_CHG : SOC_INTEGRAL_DIR_DSG;
    if (magnitude_a10 != 0)
    {
        uint32_t a10 = magnitude_ma / 100u;
        *magnitude_a10 = (a10 > 65535u) ? 65535u : (uint16_t)a10;
    }
    return dir;
}

/* 查询当前是否处于有效充电方向。 */
static uint8_t isCHG(void)
{
    return (soc_current_direction(0) == SOC_INTEGRAL_DIR_CHG) ? 1u : 0u;
}

/* 取得内部计算的真实 SOC 百分比。 */
uint8_t get_soc_real(void)
{
    return SOC_Calculate_Element.u8SOC_Now;
}

/* 取得供上位机显示的 SOC 百分比。 */
static uint8_t get_soc_display(void)
{
    return g_soc_display_soc;
}

/* 设置显示 SOC 并限制合法范围。 */
static void set_dispsoc(uint8_t soc)
{
    g_soc_display_soc = soc_limit_percent_u32(soc);
    g_soc_display_step_ticks = 0u;
    g_stCellInfoReport.SocElement.u16Soc = g_soc_display_soc;
}

/* 按确认节拍使显示 SOC 跟随真实 SOC。 */
static void soc_display_follow_real(void)
{
    uint8_t real_soc = get_soc_real();
    uint8_t step_ticks = SOC_DISPLAY_STEP_TICKS;
    if (g_soc_display_soc == real_soc) {
        g_soc_display_step_ticks = 0u;
        return;
    }

    if (g_soc_runtime.endpoint_state == BMS_SOC_ENDPOINT_CONFIRMED_FULL)
        step_ticks = SOC_DISPLAY_CONFIRMED_FULL_TICKS;
    else if (g_soc_runtime.endpoint_state == BMS_SOC_ENDPOINT_FULL_APPROACH ||
             g_soc_runtime.endpoint_state == BMS_SOC_ENDPOINT_EMPTY_APPROACH)
        step_ticks = SOC_DISPLAY_APPROACH_STEP_TICKS;

    if (g_soc_display_step_ticks < step_ticks) g_soc_display_step_ticks++;
    if (g_soc_display_step_ticks < step_ticks) return;
    g_soc_display_step_ticks = 0u;

    if (g_soc_display_soc < real_soc) g_soc_display_soc++;
    else g_soc_display_soc--;
}

/* 计算与显示 SOC 对应的当前容量。 */
static uint32_t soc_display_capacity_now(void)
{
    return ((uint32_t)get_soc_display() * SOC_Calculate_Element.u32CapFull) / SOC_PERCENT_MAX;
}

/* 取得名义容量，单位为 0.1 Ah。 */
static uint32_t soc_nominal_capacity_0p1ah(void)
{
    bms_config_system_params_t system;
    if (bms_config_store_get_system(&system) && system.capacity_factory > 0u &&
        system.capacity_factory <= BMS_SOC_CAPACITY_MAX_0P1AH) return system.capacity_factory;
    return (uint32_t)CapacityFactory;
}

/* 按名义容量和循环 SOH 重算满容量。 */
static void soc_recalc_full_capacity(void)
{
    uint32_t factory = soc_nominal_capacity_0p1ah();
    SOC_Calculate_Element.u32CapFactory = factory * SOC_CAPACITY_UNITS_PER_FACTORY;

    SOC_Calculate_Element.soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));
    SOC_Calculate_Element.u32CapFull =
        (SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh) / SOC_PERCENT_MAX;

    if (SOC_Calculate_Element.u32CapFull == 0u) SOC_Calculate_Element.u32CapFull = 1u;
}

/* 按当前 SOC 重算剩余容量。 */
static void soc_recalc_now_capacity(void)
{
    SOC_Calculate_Element.u32CapNow =
        ((uint32_t)get_soc_real() * SOC_Calculate_Element.u32CapFull) / SOC_PERCENT_MAX;
}

/* 清除积分余量与当前方向状态。 */
static void soc_reset_integral_accumulator(void)
{
    g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
    g_soc_integral_tick_remainder = 0u;
}

/* 切换积分方向并管理方向相关余量。 */
static void soc_integral_select_dir(soc_integral_dir_t dir)
{
    if (g_soc_integral_dir != dir) {
        g_soc_integral_dir = dir;
        g_soc_integral_tick_remainder = 0u;
    }
}

/* 把电流与有效时间转换为容量积分增量。 */
static uint32_t soc_integral_delta_from_current(soc_integral_dir_t dir)
{
    uint32_t magnitude_ma;
    uint32_t ticks_left;
    uint32_t delta;
    uint32_t fractional_ma;
    const uint32_t denominator = BMS_SOC_TIME_TICKS_PER_SECOND * 100u;
    if (!g_soc_input_valid || g_soc_interval_32k == 0u) return 0u;
    soc_integral_select_dir(dir);
    magnitude_ma = (g_soc_input_current_ma < 0) ?
        (0u - (uint32_t)g_soc_input_current_ma) : (uint32_t)g_soc_input_current_ma;
    /*
     * 固定 TC32 链接器无 64 位乘除辅助函数。将幅值拆为整分母单位和有界余数块；
     * 整数结果不超过 671*12800，各次和小于 3200000*1025<UINT32_MAX。
     * 接受的 400 ms 间隔最多循环 13 次，
     * 与 mA*ticks/denominator 保持完全相同商和余数。
     */
    delta = (magnitude_ma / denominator) * g_soc_interval_32k;
    fractional_ma = magnitude_ma % denominator;
    ticks_left = g_soc_interval_32k;
    while (ticks_left != 0u)
    {
        uint32_t chunk = (ticks_left > 1024u) ? 1024u : ticks_left;
        uint32_t sum = fractional_ma * chunk + g_soc_integral_tick_remainder;
        delta += sum / denominator;
        g_soc_integral_tick_remainder = sum % denominator;
        ticks_left -= chunk;
    }
    return delta;
}

/* 将充电容量换算为 SOC 百分比。 */
static uint8_t soc_percent_from_capacity_charge(uint32_t cap)
{
    if (cap >= SOC_Calculate_Element.u32CapFull) return SOC_PERCENT_MAX;
    return soc_limit_percent_u32((cap * SOC_PERCENT_MAX) / SOC_Calculate_Element.u32CapFull);
}

/* 将放电容量换算为 SOC 百分比。 */
static uint8_t soc_percent_from_capacity_discharge(uint32_t cap)
{
    uint32_t percent;
    if (cap == 0u) return 0u;
    if (cap >= SOC_Calculate_Element.u32CapFull) return SOC_PERCENT_MAX;
    percent = ((cap * SOC_PERCENT_MAX) + SOC_Calculate_Element.u32CapFull - 1u) /
        SOC_Calculate_Element.u32CapFull;
    return soc_limit_percent_u32(percent);
}

/* 累计放电 SOC 降幅并更新等效循环。 */
static void soc_note_discharge_soc_drop(uint8_t old_soc, uint8_t new_soc)
{
    uint16_t dsg_acc;
    uint8_t cycle_changed = 0u;
    if (old_soc <= new_soc) return;

    dsg_acc = (uint16_t)SOC_Calculate_Element.u8DSG_SOC_Int + (uint16_t)(old_soc - new_soc);
    while (dsg_acc >= SOC_EQUIV_CYCLE_PERCENT) {
        dsg_acc -= SOC_EQUIV_CYCLE_PERCENT;
        if (SOC_Calculate_Element.u32Cycle_times < SOC_CYCLE_MAX) {
            SOC_Calculate_Element.u32Cycle_times += 1u;
            cycle_changed = 1u;
        }
    }
    SOC_Calculate_Element.u8DSG_SOC_Int = (uint8_t)dsg_acc;
    if (cycle_changed) {
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }
}

/* 把容量积分增量应用到真实 SOC 与循环状态。 */
static void soc_apply_integral_delta(soc_integral_dir_t dir, uint32_t delta)
{
    uint8_t old_soc;
    uint8_t new_soc;
    old_soc = get_soc_real();
    g_soc_runtime.last_integral_delta_as10 = delta;
    soc_diag_note_action(BMS_SOC_ACTION_INTEGRATE, old_soc, old_soc, 0u);
    if (delta == 0u) return;

    if (dir == SOC_INTEGRAL_DIR_CHG) {
        if ((SOC_Calculate_Element.u32CapNow >= SOC_Calculate_Element.u32CapFull) ||
            ((SOC_Calculate_Element.u32CapFull - SOC_Calculate_Element.u32CapNow) <= delta))
            SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
        else
            SOC_Calculate_Element.u32CapNow += delta;

        new_soc = soc_percent_from_capacity_charge(SOC_Calculate_Element.u32CapNow);
        if (new_soc > old_soc) {
            SOC_Calculate_Element.u8SOC_Now = new_soc;
        }
    } else if (dir == SOC_INTEGRAL_DIR_DSG) {
        if (SOC_Calculate_Element.u32CapNow <= delta) SOC_Calculate_Element.u32CapNow = 0u;
        else SOC_Calculate_Element.u32CapNow -= delta;

        new_soc = soc_percent_from_capacity_discharge(SOC_Calculate_Element.u32CapNow);
        if ((new_soc == 0u) && (old_soc > 0u) &&
            (g_soc_profile != 0) && (VCELLMIN > g_soc_profile->empty_sync_mv)) {
            SOC_Calculate_Element.u32CapNow =
                (SOC_Calculate_Element.u32CapFull + SOC_PERCENT_MAX - 1u) / SOC_PERCENT_MAX;
            new_soc = 1u;
        }
        if (new_soc < old_soc) {
            SOC_Calculate_Element.u8SOC_Now = new_soc;
            soc_note_discharge_soc_drop(old_soc, new_soc);
        }
    }
    g_soc_runtime.last_soc_after = get_soc_real();
}

/* 更新真实 SOC 并同步容量边界。 */
static void soc_apply_real_value(uint8_t soc, uint8_t sync_display)
{
    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc);
    soc_recalc_now_capacity();
    if (sync_display) set_dispsoc(SOC_Calculate_Element.u8SOC_Now);
}

/* 按规定步进向下逼近目标 SOC。 */
static uint8_t soc_step_down_to(uint8_t target_soc)
{
    uint8_t current = get_soc_real();
    target_soc = soc_limit_percent_u32(target_soc);
    if (current <= target_soc) return 0u;
    soc_apply_real_value((uint8_t)(current - 1u), 0u);
    soc_reset_integral_accumulator();
    return 1u;
}

/* 按规定步进向上逼近目标 SOC。 */
static uint8_t soc_step_up_to(uint8_t target_soc)
{
    uint8_t current = get_soc_real();
    target_soc = soc_limit_percent_u32(target_soc);
    if (current >= target_soc) return 0u;
    soc_apply_real_value((uint8_t)(current + 1u), 0u);
    soc_reset_integral_accumulator();
    return 1u;
}

/* 按策略组合最低和最高单体电压。 */
static uint16_t soc_weighted_cell_mv(void)
{
    return (uint16_t)((((uint32_t)VCELLMIN * 3u) + (uint32_t)VCELLMAX) / 4u);
}

/* 检查样本有效性及断线状态是否允许 OCV 校正。 */
static uint8_t soc_ocv_sample_valid(void)
{
    if (g_soc_profile == 0) return 0u;
    if ((VCELLMIN < g_soc_profile->valid_min_mv) ||
        (VCELLMAX > g_soc_profile->valid_max_mv) ||
        (VCELLMAX < VCELLMIN) ||
        (g_soc_input.cell_delta_mv > SOC_OCV_CELL_DELTA_MAX_MV)) return 0u;
    return 1u;
}

/* 检查温度有效性与 OCV 允许范围。 */
static uint8_t soc_temperature_reasonable(void)
{
    if (!g_soc_input.temperature_valid) return 0u;
    if ((g_soc_input.temperature_min_x10 < SOC_OCV_TEMP_MIN_X10) ||
        (g_soc_input.temperature_max_x10 > SOC_OCV_TEMP_MAX_X10) ||
        (g_soc_input.temperature_max_x10 < g_soc_input.temperature_min_x10)) return 0u;
    return 1u;
}

/* 检查静置校正所需的保护与采样上下文。 */
static uint8_t soc_rest_context_valid(void)
{
    if (!g_soc_input.voltage_valid || !soc_temperature_reasonable() ||
        g_soc_input.balancing_active || g_soc_input.heating_active ||
        g_soc_input.open_wire_suspected ||
        g_soc_input.afe_fault || g_soc_input.temperature_fault ||
        g_soc_input.current_fault || g_soc_input.pack_fault) return 0u;
    return 1u;
}

/* 判断电流与静置条件是否允许 OCV 估算。 */
static uint8_t soc_idle_for_ocv(void)
{
    return (soc_current_direction(0) == SOC_INTEGRAL_DIR_NONE) ? 1u : 0u;
}

/* 依据当前化学体系曲线由单体电压估算 SOC。 */
static uint8_t soc_estimate_percent_from_cell_mv(uint16_t cell_mv)
{
    uint8_t i;
    const soc_ocv_point_t *points = g_soc_profile->ocv;
    uint8_t count = g_soc_profile->ocv_count;

    if (cell_mv <= points[0].mv) return points[0].soc;
    if (cell_mv >= points[count - 1u].mv) return points[count - 1u].soc;

    for (i = 1u; i < count; ++i) {
        if (cell_mv <= points[i].mv) {
            const soc_ocv_point_t *lo = &points[i - 1u];
            const soc_ocv_point_t *hi = &points[i];
            uint32_t num = ((uint32_t)(cell_mv - lo->mv) * (uint32_t)(hi->soc - lo->soc)) +
                ((uint32_t)(hi->mv - lo->mv) / 2u);
            return (uint8_t)(lo->soc + num / (uint32_t)(hi->mv - lo->mv));
        }
    }
    return SOC_PERCENT_MAX;
}

/* 更新 OCV 中心估计、误差带与置信度。 */
static void soc_update_ocv_band(uint16_t cell_mv)
{
    uint8_t center = soc_estimate_percent_from_cell_mv(cell_mv);
    uint8_t band = g_soc_config.ocv_error_band_percent;
    if (band < g_soc_profile->ocv_min_error_band_percent)
        band = g_soc_profile->ocv_min_error_band_percent;
    g_soc_runtime.ocv_mv = cell_mv;
    g_soc_runtime.ocv_center = center;
    g_soc_runtime.ocv_low = (center > band) ? (uint8_t)(center - band) : 0u;
    g_soc_runtime.ocv_high = ((uint16_t)center + band > SOC_PERCENT_MAX) ?
        SOC_PERCENT_MAX : (uint8_t)(center + band);
}

/* 取得 OCV 静置准备所需节拍数。 */
static uint32_t soc_ocv_prepare_ticks(void)
{
    return (uint32_t)g_soc_config.ocv_rest_prepare_s * SOC_TICKS_PER_SECOND;
}

/* 开发构建中记录 OCV 判定输入，不改变正式策略。 */
static void soc_trace_ocv_inputs(uint16_t event)
{
#if BMS_SOC_OCV_TRACE_ENABLE
    uint32_t flags = (uint32_t)g_soc_input.sample_valid |
        ((uint32_t)g_soc_input.voltage_valid << 1) |
        ((uint32_t)g_soc_input.temperature_valid << 2) |
        ((uint32_t)g_soc_input.balancing_active << 3) |
        ((uint32_t)g_soc_input.heating_active << 4) |
        ((uint32_t)g_soc_input.open_wire_active << 5) |
        ((uint32_t)g_soc_input.open_wire_suspected << 6) |
        ((uint32_t)g_soc_input.afe_fault << 7) |
        ((uint32_t)g_soc_input.temperature_fault << 8) |
        ((uint32_t)g_soc_input.current_fault << 9) |
        ((uint32_t)g_soc_input.pack_fault << 10) |
        ((uint32_t)g_soc_input.charger_present << 11) |
        ((uint32_t)g_soc_runtime.last_sample_state << 16) |
        ((uint32_t)g_soc_runtime.ocv_openwire_phase << 24) |
        ((uint32_t)g_soc_runtime.ocv_openwire_confirm_samples << 28);
    bms_diag_trace(event, (uint32_t)g_soc_input.current_ma, flags);
    bms_diag_trace((uint16_t)(event + 1u),
        (uint32_t)g_soc_runtime.idle_ocv_last_mv |
        ((uint32_t)g_soc_input.cell_min_mv << 16),
        (uint32_t)g_soc_input.cell_max_mv |
        ((uint32_t)g_soc_input.cell_delta_mv << 16));
    bms_diag_trace((uint16_t)(event + 2u), g_soc_input.timestamp_32k,
        (uint32_t)(g_soc_input.timestamp_32k - g_soc_runtime.ocv_openwire_started_32k));
    bms_diag_trace((uint16_t)(event + 3u),
        (uint32_t)(uint16_t)g_soc_input.temperature_min_x10 |
        ((uint32_t)(uint16_t)g_soc_input.temperature_max_x10 << 16),
        g_soc_runtime.ocv_down_ticks);
#else
    (void)event;
#endif
}

/* 复位静置、斜率和断线恢复跟踪状态。 */
static void soc_reset_ocv_tracking(void)
{
    if (g_soc_runtime.idle_stable_ticks || g_soc_runtime.ocv_down_ticks)
        soc_trace_ocv_inputs(SOC_TRACE_OCV_RESET);
    g_soc_runtime.idle_stable_ticks = 0u;
    g_soc_runtime.ocv_down_ticks = 0u;
    g_soc_runtime.ocv_openwire_phase = SOC_OCV_OPENWIRE_NONE;
    g_soc_runtime.ocv_openwire_confirm_samples = 0u;
    g_soc_runtime.idle_ocv_mv_valid = 0u;
    g_soc_runtime.ocv_confidence = 0u;
    g_soc_runtime.ocv_state = BMS_SOC_OCV_WAIT_CURRENT;
    g_soc_runtime.ocv_mv = 0u;
    g_soc_runtime.ocv_center = 0u;
    g_soc_runtime.ocv_low = 0u;
    g_soc_runtime.ocv_high = 0u;
}

/* 推进静置确认和 OCV 校正，处理断线检测暂停。 */
static uint8_t soc_idle_ocv_tracking(void)
{
    uint16_t mv;
    uint16_t diff;
    uint32_t prepare_ticks;
    uint32_t confidence;
    uint8_t current_soc;

    if (g_soc_input.open_wire_active ||
        g_soc_runtime.ocv_openwire_phase != SOC_OCV_OPENWIRE_NONE)
        soc_trace_ocv_inputs(SOC_TRACE_OCV_COW_INPUT);

    if (!soc_idle_for_ocv() || !soc_rest_context_valid()) {
        soc_reset_ocv_tracking();
        return 0u;
    }

    if (g_soc_input.open_wire_active) {
        /*
         * 只有已合格的正常基线可跨越有界诊断；失败/不确定轮次由功能所有者保持疑似，
         * 无效样本和 GAP 更早重置本状态。
         */
        if (!g_soc_runtime.idle_ocv_mv_valid ||
            g_soc_runtime.ocv_openwire_phase == SOC_OCV_OPENWIRE_RECOVERING) {
            soc_reset_ocv_tracking();
            return 0u;
        }
        if (g_soc_runtime.ocv_openwire_phase == SOC_OCV_OPENWIRE_NONE) {
            g_soc_runtime.ocv_openwire_phase = SOC_OCV_OPENWIRE_PAUSED;
            g_soc_runtime.ocv_openwire_started_32k = g_soc_input.timestamp_32k;
            g_soc_runtime.ocv_openwire_confirm_samples = 0u;
        }
        if ((uint32_t)(g_soc_input.timestamp_32k -
                       g_soc_runtime.ocv_openwire_started_32k) >
            SOC_OCV_OPENWIRE_MAX_PAUSE_32K)
            soc_reset_ocv_tracking();
        return 0u; /* COW 电压和持续时间不能作为 OCV 证据。 */
    }

    if (!soc_ocv_sample_valid() ||
        g_soc_input.cell_delta_mv > SOC_OCV_IDLE_CELL_DELTA_MAX_MV) {
        soc_reset_ocv_tracking();
        return 0u;
    }

    mv = soc_weighted_cell_mv();
    if (g_soc_runtime.ocv_openwire_phase != SOC_OCV_OPENWIRE_NONE) {
        if ((uint32_t)(g_soc_input.timestamp_32k -
                       g_soc_runtime.ocv_openwire_started_32k) >
            SOC_OCV_OPENWIRE_MAX_PAUSE_32K ||
            soc_abs_diff_u16(mv, g_soc_runtime.idle_ocv_last_mv) >
            SOC_OCV_IDLE_SLOPE_MAX_MV) {
            soc_reset_ocv_tracking();
            return 0u;
        }
        if (g_soc_runtime.ocv_openwire_phase == SOC_OCV_OPENWIRE_PAUSED) {
            g_soc_runtime.ocv_openwire_phase = SOC_OCV_OPENWIRE_RECOVERING;
            g_soc_runtime.ocv_openwire_confirm_tick_32k = g_soc_input.timestamp_32k;
            g_soc_runtime.ocv_openwire_confirm_samples = 1u;
            return 0u;
        }
        /*
         * 一个接受的 400 ms 帧可执行两次策略；资格只按新帧累计，
         * 三次确认全部完成后的下一帧才恢复。
         */
        if (g_soc_runtime.ocv_openwire_confirm_tick_32k == g_soc_input.timestamp_32k)
            return 0u;
        g_soc_runtime.ocv_openwire_confirm_tick_32k = g_soc_input.timestamp_32k;
        if (g_soc_runtime.ocv_openwire_confirm_samples < SOC_OCV_OPENWIRE_CONFIRM_SAMPLES) {
            ++g_soc_runtime.ocv_openwire_confirm_samples;
            return 0u;
        }
        g_soc_runtime.ocv_openwire_phase = SOC_OCV_OPENWIRE_NONE;
    }
    if (!g_soc_runtime.idle_ocv_mv_valid) {
        g_soc_runtime.idle_ocv_last_mv = mv;
        g_soc_runtime.idle_ocv_mv_valid = 1u;
        g_soc_runtime.ocv_state = BMS_SOC_OCV_PREPARE;
        return 0u;
    }

    diff = soc_abs_diff_u16(mv, g_soc_runtime.idle_ocv_last_mv);
    if (diff > SOC_OCV_IDLE_SLOPE_MAX_MV)
        soc_trace_ocv_inputs(SOC_TRACE_OCV_SLOPE_RESET);
    g_soc_runtime.idle_ocv_last_mv = mv;
    if (diff > SOC_OCV_IDLE_SLOPE_MAX_MV) {
        g_soc_runtime.idle_stable_ticks = 0u;
        g_soc_runtime.ocv_down_ticks = 0u;
        g_soc_runtime.ocv_confidence = 0u;
        g_soc_runtime.ocv_state = BMS_SOC_OCV_PREPARE;
        return 0u;
    }

    prepare_ticks = soc_ocv_prepare_ticks();
    if (g_soc_runtime.idle_stable_ticks < prepare_ticks)
        g_soc_runtime.idle_stable_ticks++;

    confidence = (prepare_ticks == 0u) ? 100u :
        (g_soc_runtime.idle_stable_ticks * 100u) / prepare_ticks;
    if (confidence > 100u) confidence = 100u;
    g_soc_runtime.ocv_confidence = (uint8_t)confidence;
    g_soc_runtime.ocv_state = BMS_SOC_OCV_PREPARE;

    if (g_soc_runtime.idle_stable_ticks < prepare_ticks) return 0u;

    soc_update_ocv_band(mv);
    current_soc = get_soc_real();
    g_soc_runtime.ocv_state = BMS_SOC_OCV_READY;

    /*
     * OCV 是置信区间，不是硬目标。正常 OCV 校正有意单向：可将高估 SOC 降至置信上界，
     * 但绝不升高；只有确认满充锚点可将 SOC 提升至 100%。
     */
    if (current_soc <= g_soc_runtime.ocv_high) {
        g_soc_runtime.ocv_down_ticks = 0u;
        return 0u;
    }

    g_soc_runtime.ocv_state = BMS_SOC_OCV_CORRECT_DOWN;
    if (g_soc_runtime.ocv_down_ticks < SOC_LONG_REST_DOWN_STEP_TICKS)
        g_soc_runtime.ocv_down_ticks++;
    if (g_soc_runtime.ocv_down_ticks < SOC_LONG_REST_DOWN_STEP_TICKS) return 0u;

    g_soc_runtime.ocv_down_ticks = 0u;
    if (soc_step_down_to(g_soc_runtime.ocv_high)) {
        soc_diag_note_action(BMS_SOC_ACTION_OCV_DOWN, current_soc,
                             g_soc_runtime.ocv_high,
                             g_soc_runtime.ocv_confidence);
        return 1u;
    }
    return 0u;
}

/* 按电流和容量计算自然放电 1% 所需节拍。 */
static uint16_t soc_discharge_natural_1pct_ticks(uint16_t dsg_current)
{
    uint16_t factory_a10;
    uint32_t ticks;
    if (dsg_current < SOC_DSG_CURRENT_MIN_A10) dsg_current = SOC_DSG_CURRENT_MIN_A10;
    factory_a10 = (uint16_t)soc_nominal_capacity_0p1ah();
    /*
     * CapacityFactory 单位 Ah*10，电流 A*10；
     * 1% 所需秒数为36*CapacityFactory/current。
     */
    ticks = ((uint32_t)36u * factory_a10 * SOC_TICKS_PER_SECOND + ((uint32_t)dsg_current / 2u)) /
        (uint32_t)dsg_current;
    if (ticks < SOC_DSG_NATURAL_STEP_MIN_TICKS) ticks = SOC_DSG_NATURAL_STEP_MIN_TICKS;
    if (ticks > SOC_DSG_NATURAL_STEP_MAX_TICKS) ticks = SOC_DSG_NATURAL_STEP_MAX_TICKS;
    return (uint16_t)ticks;
}

/* 按 SOC 偏差计算放电校正步进间隔。 */
static uint16_t soc_discharge_gap_correction_step_ticks(uint8_t current_soc,
                                                        uint8_t target_soc,
                                                        uint16_t dsg_current)
{
    uint8_t gap;
    uint8_t multiplier;
    uint32_t ticks;
    if (current_soc <= target_soc) return SOC_DSG_CORR_STEP_MAX_TICKS;
    gap = (uint8_t)(current_soc - target_soc);
    if (gap >= SOC_DSG_CORR_LARGE_GAP_PERCENT) multiplier = SOC_DSG_CORR_LARGE_GAP_MUL;
    else if (gap >= SOC_DSG_CORR_MID_GAP_PERCENT) multiplier = SOC_DSG_CORR_MID_GAP_MUL;
    else multiplier = SOC_DSG_CORR_SMALL_GAP_MUL;

    ticks = (uint32_t)soc_discharge_natural_1pct_ticks(dsg_current) * multiplier;
    if (ticks < SOC_DSG_CORR_STEP_MIN_TICKS) ticks = SOC_DSG_CORR_STEP_MIN_TICKS;
    if (ticks > SOC_DSG_CORR_STEP_MAX_TICKS) ticks = SOC_DSG_CORR_STEP_MAX_TICKS;
    return (uint16_t)ticks;
}

/* 根据大电流压降更新放电校正保持期。 */
static void soc_update_discharge_sag_hold(void)
{
    uint16_t current = 0u;
    soc_integral_dir_t dir = soc_current_direction(&current);
    if ((dir == SOC_INTEGRAL_DIR_DSG) && (current > SOC_DSG_SAG_HOLD_CURR_MIN_A10)) {
        uint16_t hold = (current > 100u) ? SOC_DSG_SAG_HOLDOFF_HIGH_TICKS : SOC_DSG_SAG_HOLDOFF_TICKS;
        if (g_soc_runtime.dsg_sag_hold_ticks < hold) g_soc_runtime.dsg_sag_hold_ticks = hold;
    } else if (g_soc_runtime.dsg_sag_hold_ticks > 0u) {
        g_soc_runtime.dsg_sag_hold_ticks--;
    }
}

/* 查询压降保持期是否仍有效。 */
static uint8_t soc_discharge_sag_hold_active(void)
{
    return (g_soc_runtime.dsg_sag_hold_ticks > 0u) ? 1u : 0u;
}

/* 取得当前欠压触发阈值，单位为毫伏。 */
static uint16_t soc_uvp_trip_mv(void)
{
    uint16_t uvp = g_bms_protection_params.u16VcellUvp_Third;
    if ((uvp < 2200u) || (uvp > 3500u)) uvp = g_soc_profile->empty_sync_mv;
    return uvp;
}

/* 计算有符号电流的绝对量。 */
/* 复位 SOC 内部剩余时间估算状态。 */
static void soc_eta_reset(void)
{
    bms_soc_eta_reset(&g_soc_runtime.eta);
}

/* 用当前有效容量、电流和方向更新剩余时间。 */
static void soc_eta_update(void)
{
    soc_integral_dir_t direction = soc_current_direction(0);
    bms_soc_eta_input_t input;
    input.current_ma = g_soc_input_current_ma;
    input.remaining_as10 = SOC_Calculate_Element.u32CapNow;
    input.full_as10 = SOC_Calculate_Element.u32CapFull;
    input.deadband_ma = g_soc_config.current_deadband_ma;
    if (input.deadband_ma < BMS_CURRENT_UNRELIABLE_MAX_MA)
        input.deadband_ma = BMS_CURRENT_UNRELIABLE_MAX_MA;
    input.direction = direction == SOC_INTEGRAL_DIR_CHG ? BMS_SOC_ETA_DIR_CHARGE :
        (direction == SOC_INTEGRAL_DIR_DSG ? BMS_SOC_ETA_DIR_DISCHARGE : BMS_SOC_ETA_DIR_NONE);
    input.endpoint_active = g_soc_runtime.endpoint_state != BMS_SOC_ENDPOINT_NORMAL;
    input.near_full = VCELLMAX + g_soc_profile->full_min_margin_mv >= g_soc_profile->full_sync_mv;
    bms_soc_eta_update(&g_soc_runtime.eta, &input);
}

/* 查找放电末端电压对应的 SOC 约束。 */
static uint8_t soc_terminal_lookup(uint8_t *target_soc, uint8_t *sag_hold_blocks)
{
    uint16_t uvp = soc_uvp_trip_mv();
    if ((target_soc == 0) || (sag_hold_blocks == 0) || (VCELLMAX < VCELLMIN)) return 0u;

    if (VCELLMIN <= uvp) {
        /*
         * 原始电压达到/低于 UVP 本身不是保护结论；大电流压降在此仍保持，
         * 独立滤波的 Third Cell UVP 仍强制最终安全锚点。
         */
        *target_soc = 0u; *sag_hold_blocks = 1u; return 1u;
    }
    if (VCELLMIN <= (uint16_t)(uvp + g_soc_profile->terminal_l3_offset_mv)) {
        *target_soc = 1u; *sag_hold_blocks = 0u; return 1u;
    }
    if (VCELLMIN <= (uint16_t)(uvp + g_soc_profile->terminal_l2_offset_mv)) {
        *target_soc = 3u; *sag_hold_blocks = 1u; return 1u;
    }
    if (VCELLMIN <= (uint16_t)(uvp + g_soc_profile->terminal_l1_offset_mv)) {
        *target_soc = 6u; *sag_hold_blocks = 1u; return 1u;
    }
    if (VCELLMIN <= (uint16_t)(uvp + g_soc_profile->terminal_start_offset_mv)) {
        *target_soc = 12u; *sag_hold_blocks = 1u; return 1u;
    }
    return 0u;
}

/* 按末端电压与压降资格推进放电 SOC 修正。 */
static uint8_t soc_apply_discharge_terminal_tracking(void)
{
    uint8_t target_soc;
    uint8_t sag_hold_blocks;
    uint8_t current_soc;
    uint16_t current = 0u;
    uint16_t step_ticks;

    if (soc_current_direction(&current) != SOC_INTEGRAL_DIR_DSG) {
        g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
        g_soc_runtime.dsg_empty_lock_ticks = 0u;
        return 0u;
    }
    if (!soc_terminal_lookup(&target_soc, &sag_hold_blocks)) {
        g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
        g_soc_runtime.dsg_empty_lock_ticks = 0u;
        return 0u;
    }
    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_EMPTY_APPROACH;
    if (sag_hold_blocks && soc_discharge_sag_hold_active()) {
        g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
        return 0u;
    }

    current_soc = get_soc_real();
    if (current_soc <= target_soc) {
        g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
        g_soc_runtime.dsg_empty_lock_ticks = 0u;
        return 0u;
    }

    if (target_soc == 0u) {
        if (g_soc_runtime.dsg_empty_lock_ticks < SOC_DSG_EMPTY_LOCK_TICKS)
            g_soc_runtime.dsg_empty_lock_ticks++;
        if (g_soc_runtime.dsg_empty_lock_ticks >= SOC_DSG_EMPTY_LOCK_TICKS) {
            soc_apply_real_value(0u, 0u);
            soc_reset_integral_accumulator();
            soc_diag_note_action(BMS_SOC_ACTION_TERMINAL_DOWN,
                                 current_soc, 0u, sag_hold_blocks);
            if (!g_soc_runtime.empty_anchor_latched) {
                g_soc_runtime.empty_anchor_latched = 1u;
            }
            g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
            return 1u;
        }
        return 0u;
    }

    g_soc_runtime.dsg_empty_lock_ticks = 0u;
    step_ticks = soc_discharge_gap_correction_step_ticks(current_soc, target_soc, current);
    if (g_soc_runtime.dsg_terminal_adjust_ticks < step_ticks)
        g_soc_runtime.dsg_terminal_adjust_ticks++;
    if (g_soc_runtime.dsg_terminal_adjust_ticks < step_ticks) return 0u;
    g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
    if (soc_step_down_to(target_soc)) {
        soc_diag_note_action(BMS_SOC_ACTION_TERMINAL_DOWN,
                             current_soc, target_soc, sag_hold_blocks);
        return 1u;
    }
    return 0u;
}

/* 确认满充端点后约束真实及显示 SOC。 */
static uint8_t soc_apply_full_anchor(void)
{
    /* 只有确认真实充电方向时才允许向上校准；空闲、高压启动和电压回弹绝不能提高 SOC。 */
    uint16_t full_mv = g_soc_profile->full_sync_mv;
    uint16_t full_min = (full_mv > g_soc_profile->full_min_margin_mv) ?
        (uint16_t)(full_mv - g_soc_profile->full_min_margin_mv) : 0u;
    uint8_t voltage_ready = (VCELLMAX >= full_mv) && (VCELLMIN >= full_min) &&
        (g_soc_input.cell_delta_mv <= g_soc_profile->full_cell_delta_max_mv) && isCHG();
    uint8_t before;

    if (isCHG() && g_soc_input.third_cell_ovp) {
        before = get_soc_real();
        if (get_soc_real() != SOC_PERCENT_MAX) {
            soc_apply_real_value(SOC_PERCENT_MAX, 0u);
            soc_reset_integral_accumulator();
        }
        if (!g_soc_runtime.full_anchor_latched) {
            g_soc_runtime.full_anchor_latched = 1u;
            g_soc_runtime.empty_anchor_latched = 0u;
        }
        g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_FULL;
        soc_diag_note_action(BMS_SOC_ACTION_FULL_ANCHOR, before,
                             SOC_PERCENT_MAX, 1u);
        return 1u;
    }

    if (!voltage_ready) {
        g_soc_runtime.full_lock_ticks = 0u;
        g_soc_runtime.full_adjust_ticks = 0u;
        if (VCELLMAX + 100u < full_mv) g_soc_runtime.full_anchor_latched = 0u;
        return 0u;
    }

    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_FULL_APPROACH;

    if (g_soc_runtime.full_lock_ticks < SOC_FULL_LOCK_TICKS) g_soc_runtime.full_lock_ticks++;
    if (g_soc_runtime.full_lock_ticks < SOC_FULL_LOCK_TICKS) return 0u;

    if (get_soc_real() >= SOC_PERCENT_MAX) {
        if (!g_soc_runtime.full_anchor_latched) {
            g_soc_runtime.full_anchor_latched = 1u;
            g_soc_runtime.empty_anchor_latched = 0u;
        }
        g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_FULL;
        return 0u;
    }

    if (g_soc_runtime.full_adjust_ticks < SOC_FULL_SYNC_STEP_TICKS)
        g_soc_runtime.full_adjust_ticks++;
    if (g_soc_runtime.full_adjust_ticks < SOC_FULL_SYNC_STEP_TICKS) return 0u;
    g_soc_runtime.full_adjust_ticks = 0u;
    before = get_soc_real();
    if (soc_step_up_to(SOC_PERCENT_MAX)) {
        if (get_soc_real() == SOC_PERCENT_MAX && !g_soc_runtime.full_anchor_latched) {
            g_soc_runtime.full_anchor_latched = 1u;
            g_soc_runtime.empty_anchor_latched = 0u;
        }
        if (get_soc_real() == SOC_PERCENT_MAX)
            g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_FULL;
        soc_diag_note_action(BMS_SOC_ACTION_FULL_ANCHOR, before,
                             SOC_PERCENT_MAX, 2u);
        return 1u;
    }
    return 0u;
}

/* 在明确空电条件下执行强制空电锚定。 */
static uint8_t soc_apply_forced_empty_anchor(void)
{
    uint8_t before;
    if (!g_soc_input.third_cell_uvp) return 0u;
    before = get_soc_real();
    if (before > 5u) g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_EARLY_UVP;
    if (before > 10u) g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_CAPACITY_MISMATCH;
    if (soc_discharge_sag_hold_active())
        g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_LARGE_SAG;
    if (g_soc_input.cell_delta_mv > g_soc_profile->terminal_cell_delta_max_mv)
        g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_IMBALANCE;
    if (get_soc_real() != 0u) {
        soc_apply_real_value(0u, 1u);
        soc_reset_integral_accumulator();
    }
    soc_diag_note_action(BMS_SOC_ACTION_FORCED_EMPTY, before, 0u,
                         g_soc_runtime.endpoint_event_flags);
    if (!g_soc_runtime.empty_anchor_latched) {
        g_soc_runtime.empty_anchor_latched = 1u;
        g_soc_runtime.full_anchor_latched = 0u;
    }
    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
    return 1u;
}

/* 在合格静置低电压条件下执行空电锚定。 */
static uint8_t soc_apply_idle_empty_anchor(void)
{
    uint16_t empty_mv = g_soc_profile->empty_sync_mv;
    uint16_t empty_max = (uint16_t)(empty_mv + g_soc_profile->empty_max_margin_mv);
    uint8_t before;
    if (!soc_idle_for_ocv() || (VCELLMIN > empty_mv) || (VCELLMAX > empty_max)) {
        g_soc_runtime.empty_lock_ticks = 0u;
        g_soc_runtime.empty_adjust_ticks = 0u;
        if (VCELLMIN > empty_mv + 100u) g_soc_runtime.empty_anchor_latched = 0u;
        return 0u;
    }

    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_EMPTY_APPROACH;

    if (g_soc_runtime.empty_lock_ticks < SOC_EMPTY_LOCK_TICKS) g_soc_runtime.empty_lock_ticks++;
    if (g_soc_runtime.empty_lock_ticks < SOC_EMPTY_LOCK_TICKS) return 0u;
    if (get_soc_real() == 0u) {
        if (!g_soc_runtime.empty_anchor_latched) {
            g_soc_runtime.empty_anchor_latched = 1u;
            g_soc_runtime.full_anchor_latched = 0u;
        }
        g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
        return 0u;
    }

    if (g_soc_runtime.empty_adjust_ticks < SOC_EMPTY_SYNC_STEP_TICKS)
        g_soc_runtime.empty_adjust_ticks++;
    if (g_soc_runtime.empty_adjust_ticks < SOC_EMPTY_SYNC_STEP_TICKS) return 0u;
    g_soc_runtime.empty_adjust_ticks = 0u;
    before = get_soc_real();
    if (soc_step_down_to(0u)) {
        if (get_soc_real() == 0u && !g_soc_runtime.empty_anchor_latched) {
            g_soc_runtime.empty_anchor_latched = 1u;
            g_soc_runtime.full_anchor_latched = 0u;
        }
        if (get_soc_real() == 0u)
            g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
        soc_diag_note_action(BMS_SOC_ACTION_IDLE_EMPTY, before, 0u, 0u);
        return 1u;
    }
    return 0u;
}

/* 将低 SOC 保护延时转换为确认样本数。 */
static uint16_t soc_fault_filter_samples(void)
{
    uint32_t ms = (uint32_t)g_bms_protection_params.u16SocLow_Filter * 10u;
    uint32_t samples = (ms + SOC_INTEGRAL_PERIOD_MS - 1u) / SOC_INTEGRAL_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

/* 取得指定级别的 SOC 故障位寄存器。 */
static union MDLCHGFAULT_REG *soc_fault_reg(uint8_t level)
{
    if (level == 0u) return &g_stCellInfoReport.unMdlFault_First;
    if (level == 1u) return &g_stCellInfoReport.unMdlFault_Second;
    return &g_stCellInfoReport.unMdlFault_Third;
}

/* 取得指定级别的低 SOC 阈值。 */
static uint16_t soc_fault_threshold(uint8_t level)
{
    if (level == 0u) return g_bms_protection_params.u16SocLow_First;
    if (level == 1u) return g_bms_protection_params.u16SocLow_Second;
    return g_bms_protection_params.u16SocLow_Third;
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
        uint16_t recover = g_bms_protection_params.u16SocLow_Rcv;

        if (trip == 0u || trip > SOC_PERCENT_MAX) {
            g_soc_runtime.soc_low_active[level] = 0u;
            g_soc_runtime.soc_low_trip_count[level] = 0u;
            g_soc_runtime.soc_low_recover_count[level] = 0u;
            soc_fault_reg(level)->bits.b1SocLow = 0u;
            continue;
        }
        if (recover <= trip) recover = (trip < SOC_PERCENT_MAX) ? (uint16_t)(trip + 1u) : SOC_PERCENT_MAX;

        if (g_soc_runtime.soc_low_active[level]) {
            if (soc >= recover) {
                if (g_soc_runtime.soc_low_recover_count[level] < needed)
                    g_soc_runtime.soc_low_recover_count[level]++;
                if (g_soc_runtime.soc_low_recover_count[level] >= needed) {
                    g_soc_runtime.soc_low_active[level] = 0u;
                    g_soc_runtime.soc_low_recover_count[level] = 0u;
                }
            } else {
                g_soc_runtime.soc_low_recover_count[level] = 0u;
            }
        } else {
            if (soc <= trip) {
                if (g_soc_runtime.soc_low_trip_count[level] < needed)
                    g_soc_runtime.soc_low_trip_count[level]++;
                if (g_soc_runtime.soc_low_trip_count[level] >= needed) {
                    g_soc_runtime.soc_low_active[level] = 1u;
                    g_soc_runtime.soc_low_trip_count[level] = 0u;
                    bms_fault_history_record(soc_fault_history_code(level));
                }
            } else {
                g_soc_runtime.soc_low_trip_count[level] = 0u;
            }
        }
        soc_fault_reg(level)->bits.b1SocLow = g_soc_runtime.soc_low_active[level];
    }
}

/* 推进 OCV、充放电端点与显示 SOC 策略。 */
static void soc_strategy_update(void)
{
    soc_profile_refresh();
    /*
     * 即使满充后电流立即消失，也保持 CONFIRMED_FULL 足够久，
     * 让显示 SOC 完成平缓到达。
     */
    g_soc_runtime.endpoint_state =
        (g_soc_runtime.full_anchor_latched && get_soc_real() == SOC_PERCENT_MAX &&
         get_soc_display() < SOC_PERCENT_MAX) ?
        BMS_SOC_ENDPOINT_CONFIRMED_FULL : BMS_SOC_ENDPOINT_NORMAL;
    soc_update_discharge_sag_hold();

    if (soc_apply_full_anchor()) { soc_eta_update(); return; }
    if (soc_apply_forced_empty_anchor()) { soc_eta_update(); return; }

    if (soc_apply_discharge_terminal_tracking()) { soc_eta_update(); return; }
    if (soc_apply_idle_empty_anchor()) { soc_eta_update(); return; }
    (void)soc_idle_ocv_tracking();
    soc_eta_update();
}

/* 设置计算 SOC 并处理外部状态变更。 */
static void set_calsoc(uint8_t soc)
{
    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc);
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
}

/* 更新 SOC 参数与相关容量状态。 */
void set_soc_param(uint8_t soc, uint8_t sync_display)
{
    uint8_t before = get_soc_real();
    set_calsoc(soc);
    soc_invalidate_sample_interval();
    soc_reset_integral_accumulator();
    soc_reset_ocv_tracking();
    if (sync_display) set_dispsoc(get_soc_real());
    soc_recalc_now_capacity();
    soc_diag_note_action(BMS_SOC_ACTION_PARAMETER_SET, before,
                         soc_limit_percent_u32(soc), sync_display);
}

/* 初始化 SOC 参数、持久状态与策略计数。 */
void soc_param_lib_init(const bms_state_store_data_t *soc)
{
    bms_state_store_data_t defaults;
    memset(&g_soc_runtime, 0, sizeof(g_soc_runtime));
    soc_invalidate_sample_interval();
    soc_load_persisted_product_config();
    soc_profile_refresh();

    if (soc == 0) {
        defaults = bms_state_store_get_default_data();
        soc = &defaults;
    }

    SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(soc->dsg);
    SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(soc->cycle);
    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc->soc);
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
    set_dispsoc(get_soc_real());
    soc_reset_integral_accumulator();
    soc_reset_ocv_tracking();
    soc_eta_reset();
    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_NORMAL;
    soc_diag_note_action(BMS_SOC_ACTION_STATE_RESTORE,
                         SOC_Calculate_Element.u8SOC_Now,
                         SOC_Calculate_Element.u8SOC_Now, 0u);

    g_soc_initialized = 1u;
    SOC_Result_Pass();
}

/* 对合格时间区间的电流进行容量积分。 */
static void soc_integrate_current(soc_integral_dir_t dir)
{
    if (dir == SOC_INTEGRAL_DIR_NONE) {
        soc_reset_integral_accumulator();
        return;
    }
    soc_apply_integral_delta(dir, soc_integral_delta_from_current(dir));
}

/* 把 SOC、容量、循环和 SOH 结果发布到公共报告。 */
static void SOC_Result_Pass(void)
{
    soc_display_follow_real();
    g_stCellInfoReport.SocElement.u16Soc = get_soc_display();
    g_stCellInfoReport.SocElement.u16Soh = SOC_Calculate_Element.soh;
    g_stCellInfoReport.SocElement.u16Cycle_times = soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times);

    g_stCellInfoReport.SocElement.u16CapacityNow =
        (uint16_t)(soc_display_capacity_now() / SOC_REPORT_CAPACITY_DIVISOR);
    g_stCellInfoReport.SocElement.u16CapacityFull =
        (uint16_t)(SOC_Calculate_Element.u32CapFull / SOC_REPORT_CAPACITY_DIVISOR);
    g_stCellInfoReport.SocElement.u16CapacityFactory =
        (uint16_t)(SOC_Calculate_Element.u32CapFactory / SOC_REPORT_CAPACITY_DIVISOR);
}

/* 样本失效时撤销积分时间区间并复位相关跟踪。 */
static void soc_invalidate_sample_interval(void)
{
    g_soc_input_valid = 0u;
    g_soc_charger_state_ready = 0u;
    g_soc_load_state_ready = 0u;
    g_soc_interval_32k = 0u;
    g_soc_strategy_pending_32k = 0u;
    soc_reset_ocv_tracking();
    g_soc_runtime.full_lock_ticks = 0u;
    g_soc_runtime.full_adjust_ticks = 0u;
    g_soc_runtime.full_anchor_latched = 0u;
    g_soc_runtime.empty_lock_ticks = 0u;
    g_soc_runtime.empty_adjust_ticks = 0u;
    g_soc_runtime.empty_anchor_latched = 0u;
    g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
    g_soc_runtime.dsg_empty_lock_ticks = 0u;
    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_NORMAL;
    soc_eta_reset();
    g_soc_display_step_ticks = 0u;
    memset(g_soc_runtime.soc_low_trip_count, 0, sizeof(g_soc_runtime.soc_low_trip_count));
    memset(g_soc_runtime.soc_low_recover_count, 0, sizeof(g_soc_runtime.soc_low_recover_count));
}

/* 外部 SOC 或容量变化后重建算法跟踪状态。 */
static uint8_t soc_external_state_changed(const bms_soc_sample_t *sample)
{
    uint8_t changed = 0u;
    if (sample->charger_state_known) {
        if (g_soc_charger_state_ready &&
            g_soc_last_charger_present != sample->charger_present) {
            changed = 1u;
        }
        g_soc_last_charger_present = sample->charger_present;
        g_soc_charger_state_ready = 1u;
    }
    if (sample->load_state_known) {
        if (g_soc_load_state_ready &&
            g_soc_last_load_present != sample->load_present) {
            changed = 1u;
        }
        g_soc_last_load_present = sample->load_present;
        g_soc_load_state_ready = 1u;
    }
    return changed;
}

/* 检查样本与时间差后执行积分和 SOC 策略。 */
void bms_soc_process_sample(const bms_soc_sample_t *sample)
{
    uint32_t elapsed_32k;
    const uint32_t quantum_32k = BMS_SOC_TIME_TICKS_PER_SECOND / SOC_TICKS_PER_SECOND;
    soc_integral_dir_t dir;
    soc_integral_dir_t previous_dir;
    uint8_t external_change;

    if (sample == 0) {
        soc_diag_note_sample(BMS_SOC_SAMPLE_INVALID, SOC_INTEGRAL_DIR_NONE, 0u);
        soc_invalidate_sample_interval();
        return;
    }
    g_soc_input = *sample;
    external_change = soc_external_state_changed(sample);

    if (!g_soc_initialized || !sample->sample_valid || !sample->voltage_valid)
    {
        soc_diag_note_sample(BMS_SOC_SAMPLE_INVALID, SOC_INTEGRAL_DIR_NONE, 0u);

        soc_invalidate_sample_interval();
        return;
    }
    if (!g_soc_input_valid)
    {
        soc_diag_note_sample(BMS_SOC_SAMPLE_FIRST,
                             SOC_INTEGRAL_DIR_NONE, 0u);
        g_soc_sample_tick_32k = sample->timestamp_32k;
        g_soc_input_current_ma = sample->current_ma;
        g_soc_input_valid = 1u;
        return; /* 首个新样本不能证明此前时间段。 */
    }
    elapsed_32k = sample->timestamp_32k - g_soc_sample_tick_32k;
    if (elapsed_32k == 0u) {
        soc_diag_note_sample(BMS_SOC_SAMPLE_DUPLICATE,
                             soc_current_direction(0), 0u);
        return; /* 重复缓存读取不是新证据。 */
    }
    g_soc_sample_tick_32k = sample->timestamp_32k;
    if (elapsed_32k > BMS_SOC_MAX_SAMPLE_GAP_32K)
    {
        soc_diag_note_sample(BMS_SOC_SAMPLE_GAP,
                             SOC_INTEGRAL_DIR_NONE, elapsed_32k);

        soc_invalidate_sample_interval();
        /* 当前帧开启新时间段，不补填盲区。 */
        g_soc_sample_tick_32k = sample->timestamp_32k;
        g_soc_input_current_ma = sample->current_ma;
        g_soc_input_valid = 1u;
        return;
    }
    previous_dir = soc_current_direction(0);
    g_soc_input_current_ma = sample->current_ma;
    g_soc_input_valid = 1u;
    g_soc_interval_32k = elapsed_32k;
    dir = soc_current_direction(0);
    soc_diag_note_sample(BMS_SOC_SAMPLE_ACCEPTED, dir, elapsed_32k);

    if (external_change) {
        soc_reset_ocv_tracking();
        g_soc_runtime.full_lock_ticks = 0u;
        g_soc_runtime.full_adjust_ticks = 0u;
        g_soc_runtime.empty_lock_ticks = 0u;
        g_soc_runtime.empty_adjust_ticks = 0u;
    }
    soc_integrate_current(dir);

    if (dir != previous_dir)
    {
        g_soc_runtime.last_sample_state = BMS_SOC_SAMPLE_DIRECTION_CHANGE;
        /* 跨电流状态转换的时间段不能证明连续静置或连续满/空锚点条件。 */
        soc_reset_ocv_tracking();
        g_soc_runtime.full_lock_ticks = 0u;
        g_soc_runtime.full_adjust_ticks = 0u;
        g_soc_runtime.empty_lock_ticks = 0u;
        g_soc_runtime.empty_adjust_ticks = 0u;
        g_soc_runtime.dsg_empty_lock_ticks = 0u;
        g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
        soc_eta_reset();
        g_soc_strategy_pending_32k = 0u;
        g_soc_interval_32k = 0u;
        return;
    }

    /*
     * 既有校准阈值仍为 200 ms 单位，但按实测时间计入；
     * 每个有界间隔新样本最多记两个单位。长间隔下 8 mV 斜率检查保持保守。
     */
    g_soc_strategy_pending_32k += elapsed_32k;
    while (g_soc_strategy_pending_32k >= quantum_32k)
    {
        g_soc_strategy_pending_32k -= quantum_32k;
        soc_strategy_update();
        soc_update_low_faults();
        SOC_Result_Pass();
    }
    g_soc_interval_32k = 0u; /* 每个采样间隔只积分一次 */
}

/* 名义容量变更后重算容量并重建积分与显示状态。 */
void bms_soc_nominal_capacity_changed(void)
{
    soc_recalc_full_capacity();
    set_soc_param(get_soc_real(), 1u);
    SOC_Result_Pass();
}
