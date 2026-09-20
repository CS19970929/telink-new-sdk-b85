#include "SocEnhance.h"
#include "bms_config_store.h"
#include "bms_soc_profile.h"
#include "bms_cold_kv_store.h"
#include "bms_features.h"
#include "bms_error.h"
#include "bms_state.h"
#include "param.h"
#include <string.h>

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
#define SOC_LEARNED_CAP_MIN_PERCENT          50u
#define SOC_LEARNED_CAP_MAX_PERCENT          130u
#define SOC_LEARNING_CANDIDATE_TOLERANCE_PERCENT 5u
#define SOC_LEARNING_UPDATE_MAX_PERCENT      5u
#define SOC_LEARNING_CONFIRM_CYCLES          2u
#define SOC_ETA_FILTER_SHIFT                 3u
#define SOC_ETA_STABLE_SECONDS               30u
#define SOC_ETA_STABLE_TICKS                 (SOC_TICKS_PER_SECOND * SOC_ETA_STABLE_SECONDS)
#define SOC_ETA_VARIATION_MIN_MA             250u
#define SOC_ETA_VARIATION_PERCENT            20u
#define SOC_ETA_TAPER_PERCENT                60u
#define SOC_ENDPOINT_EVENT_EARLY_UVP         0x01u
#define SOC_ENDPOINT_EVENT_LARGE_SAG         0x02u
#define SOC_ENDPOINT_EVENT_IMBALANCE         0x04u
#define SOC_ENDPOINT_EVENT_CAPACITY_MISMATCH 0x08u
#define SOC_ENDPOINT_EVENT_LEARNING_REJECTED 0x10u
#define SOC_LEARNING_TEMP_FAULT_MASK         0x2BC0u
#define SOC_LEARNING_CURRENT_FAULT_MASK      0x0030u
#define SOC_LEARNING_PACK_FAULT_MASK         0x000Cu
#define SOC_OCV_TEMP_MIN_X10                 200u  /* -20 degC */
#define SOC_OCV_TEMP_MAX_X10                 1000u /* +60 degC */

#ifndef BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT
#define BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u
#endif
#ifndef BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT
#define BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT 1u
#endif
#ifndef BMS_CURRENT_UNRELIABLE_MAX_MA
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
#endif

typedef enum
{
    SOC_INTEGRAL_DIR_NONE = 0,
    SOC_INTEGRAL_DIR_CHG,
    SOC_INTEGRAL_DIR_DSG,
} soc_integral_dir_t;

typedef enum
{
    SOC_CALI_STATE_TRANSFER = 0,
    SOC_CALI_CONT_CHG,
    SOC_CALI_CONT_DSG,
} soc_cali_state_t;

typedef struct
{
    uint32_t idle_stable_ticks;
    uint32_t ocv_down_ticks;
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

    uint8_t capacity_learned;
    uint16_t learned_capacity_0p1ah;
    uint8_t learning_state;
    uint32_t learning_capacity_as10;
    uint16_t candidate_capacity_0p1ah;
    uint16_t valid_learning_count;
    uint16_t rejected_learning_count;
    uint8_t candidate_match_count;
    uint8_t last_learning_reject_reason;
    uint8_t learning_confidence;
    int32_t learning_current_offset_ma;
    uint32_t learning_current_gain_ppm;

    int32_t eta_filtered_current_ma;
    uint32_t eta_variation_ma;
    uint32_t eta_peak_current_ma;
    uint16_t eta_stable_ticks;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
    uint8_t eta_state;
    uint8_t eta_direction;
    uint8_t eta_confidence;
    uint8_t eta_valid;

    uint8_t endpoint_state;
    uint8_t endpoint_event_flags;
    uint8_t soh_source;
    uint8_t soh_confidence;

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
static soc_cali_state_t SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
static soc_runtime_t g_soc_runtime;
static soc_integral_dir_t g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
/* mA * 32k-ticks remainder, denominator 100 mA per As*10 unit. */
static uint32_t g_soc_integral_tick_remainder;
static int32_t g_soc_input_current_ma;
static bms_soc_sample_t g_soc_input;
static uint32_t g_soc_sample_tick_32k;
static uint32_t g_soc_interval_32k;
static uint32_t g_soc_strategy_pending_32k;
static uint8_t g_soc_input_valid;
static uint8_t g_soc_input_ready;
static uint8_t g_soc_charger_state_ready;
static uint8_t g_soc_last_charger_present;
static uint8_t g_soc_load_state_ready;
static uint8_t g_soc_last_load_present;
static uint8_t g_soc_display_soc = (uint8_t)SOC_PARAM_DEFAULT_SOC;
static uint8_t g_soc_display_step_ticks;
static uint8_t g_soc_initialized;
static bms_soc_config_t g_soc_config = {
    BMS_SOC_CHEMISTRY_AUTO,
    BMS_SOC_PROFILE_AUTO,
    SOC_CURRENT_DEADBAND_MA_DEFAULT,
    SOC_OCV_REST_PREPARE_SECONDS,
    SOC_OCV_ERROR_BAND_PERCENT,
    BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT,
    BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT,
};
static const soc_profile_t *g_soc_profile;

static void soc_recalc_full_capacity(void);
static void soc_recalc_now_capacity(void);
static void soc_reset_ocv_tracking(void);
static void soc_profile_refresh(void);
static void soc_invalidate_sample_interval(void);
static void soc_learning_abort(void);
static void soc_learning_persist(void);
static uint8_t soc_learning_full_quality(void);
static uint8_t soc_learning_empty_quality(void);

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

static void soc_diag_note_action(uint8_t action, uint8_t before,
                                 uint8_t target, uint8_t detail)
{
    g_soc_runtime.last_soc_action = action;
    g_soc_runtime.last_soc_before = before;
    g_soc_runtime.last_soc_after = get_soc_real();
    g_soc_runtime.last_soc_target = target;
    g_soc_runtime.last_decision_detail = detail;
}

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

void bms_soc_get_default_config(bms_soc_config_t *config)
{
    if (config == 0) return;
    config->chemistry = BMS_SOC_CHEMISTRY_AUTO;
    config->profile_id = BMS_SOC_PROFILE_AUTO;
    config->current_deadband_ma = SOC_CURRENT_DEADBAND_MA_DEFAULT;
    config->ocv_rest_prepare_s = SOC_OCV_REST_PREPARE_SECONDS;
    config->ocv_error_band_percent = SOC_OCV_ERROR_BAND_PERCENT;
    config->capacity_learning_enable = BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT;
    config->hide_capacity_until_learned = BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT;
}

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

uint8_t bms_soc_config_valid(const bms_soc_config_t *config)
{
    if (config == 0) return 0u;
    if (!soc_product_config_valid(config->chemistry, config->profile_id)) return 0u;
    if ((config->current_deadband_ma > 2000u) ||
        (config->ocv_rest_prepare_s < 60u) ||
        (config->ocv_rest_prepare_s > 3600u) ||
        (config->ocv_error_band_percent == 0u) ||
        (config->ocv_error_band_percent > 20u) ||
        (config->capacity_learning_enable > 1u) ||
        (config->hide_capacity_until_learned > 1u)) return 0u;
    return 1u;
}

uint8_t bms_soc_configure(const bms_soc_config_t *config)
{
    if (!bms_soc_config_valid(config)) return 0u;

    if (!bms_config_store_set_soc(config)) return 0u;
    g_soc_config = *config;
    if (!g_soc_config.capacity_learning_enable &&
        g_soc_runtime.learning_state != BMS_SOC_LEARNING_NONE) {
        soc_learning_abort();
        soc_learning_persist();
    }
    soc_invalidate_sample_interval();
    soc_profile_refresh();
    soc_reset_ocv_tracking();
    if (g_soc_initialized) {
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }
    return 1u;
}

static const soc_profile_t *soc_profile_from_id(uint8_t profile_id)
{
    if (profile_id == BMS_SOC_PROFILE_GENERIC_LFP) return &g_soc_profile_lfp;
    if (profile_id == BMS_SOC_PROFILE_GENERIC_NMC) return &g_soc_profile_nmc;
    return 0;
}

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

    /* AUTO is an explicit generic selection. D008 defaults use the compiled
     * assembly identity; no old Flash migration is performed. */
    ovp = g_tParam.protect.u16VcellOvp_Third;
    if ((ovp >= 3300u) && (ovp <= SOC_AUTO_LFP_OVP_MAX_MV)) return BMS_SOC_CHEMISTRY_LFP;
    if ((ovp > SOC_AUTO_LFP_OVP_MAX_MV) && (ovp <= 4500u)) return BMS_SOC_CHEMISTRY_NMC;

    return BMS_SOC_CHEMISTRY_NMC;
}

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

static void soc_load_persisted_product_config(void)
{
    (void)bms_config_store_get_soc(&g_soc_config);
}

uint8_t bms_soc_set_product_config(uint8_t chemistry, uint8_t profile_id)
{
    bms_soc_config_t next = g_soc_config;
    next.chemistry = chemistry;
    next.profile_id = profile_id;
    return bms_soc_configure(&next);
}

void bms_soc_refresh_profile_from_params(void)
{
    soc_profile_refresh();
}

uint8_t bms_soc_get_chemistry(void)
{
    if (g_soc_profile == 0) soc_profile_refresh();
    return g_soc_profile->chemistry;
}

void bms_soc_get_diag(bms_soc_diag_t *diag)
{
    uint32_t rest_s;
    if (diag == 0) return;
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
    diag->capacity_learned = g_soc_runtime.capacity_learned;
    diag->learning_state = g_soc_runtime.learning_state;
    diag->ocv_cell_mv = g_soc_runtime.ocv_mv;
    diag->rest_seconds = (uint16_t)rest_s;
    diag->learned_capacity_0p1ah = g_soc_runtime.learned_capacity_0p1ah;
    diag->nominal_capacity_0p1ah = (uint16_t)(SOC_Calculate_Element.u32CapFactory /
                                               SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->effective_capacity_0p1ah = (uint16_t)(SOC_Calculate_Element.u32CapFull /
                                                 SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->remaining_capacity_0p1ah = (uint16_t)(SOC_Calculate_Element.u32CapNow /
                                                 SOC_CAPACITY_UNITS_PER_FACTORY);
    diag->endpoint_state = g_soc_runtime.endpoint_state;
    diag->endpoint_event_flags = g_soc_runtime.endpoint_event_flags;
    diag->filtered_current_ma = g_soc_runtime.eta_filtered_current_ma;
    diag->current_variation_ma = (g_soc_runtime.eta_variation_ma > 65535u) ?
        65535u : (uint16_t)g_soc_runtime.eta_variation_ma;
    diag->time_to_empty_min = g_soc_runtime.time_to_empty_min;
    diag->time_to_full_min = g_soc_runtime.time_to_full_min;
    diag->eta_state = g_soc_runtime.eta_state;
    diag->eta_direction = g_soc_runtime.eta_direction;
    diag->eta_confidence = g_soc_runtime.eta_confidence;
    diag->eta_valid = g_soc_runtime.eta_valid;
    diag->soh = SOC_Calculate_Element.soh;
    diag->soh_source = g_soc_runtime.soh_source;
    diag->soh_confidence = g_soc_runtime.soh_confidence;
    diag->capacity_learning_enable = g_soc_config.capacity_learning_enable;
    diag->capacity_learning_candidate_valid =
        (g_soc_runtime.candidate_match_count != 0u) ? 1u : 0u;
    diag->capacity_learning_confidence = g_soc_runtime.learning_confidence;
    diag->candidate_capacity_0p1ah = g_soc_runtime.candidate_capacity_0p1ah;
    diag->valid_learning_count = g_soc_runtime.valid_learning_count;
    diag->rejected_learning_count = g_soc_runtime.rejected_learning_count;
    diag->last_learning_reject_reason = g_soc_runtime.last_learning_reject_reason;
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

static uint8_t soc_limit_percent_u32(uint32_t value)
{
    return (value > SOC_PERCENT_MAX) ? SOC_PERCENT_MAX : (uint8_t)value;
}

static uint8_t soc_limit_dsg_u32(uint32_t value)
{
    return (value > SOC_DSG_INT_MAX) ? SOC_DSG_INT_MAX : (uint8_t)value;
}

static uint32_t soc_limit_cycle_u32(uint32_t value)
{
    return (value > SOC_CYCLE_MAX) ? SOC_CYCLE_MAX : value;
}

static uint16_t soc_cycle_to_u16(uint32_t value)
{
    return (uint16_t)soc_limit_cycle_u32(value);
}

static uint16_t soc_abs_diff_u16(uint16_t a, uint16_t b)
{
    return (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static soc_integral_dir_t soc_current_direction(uint16_t *magnitude_a10)
{
    uint32_t magnitude_ma;
    soc_integral_dir_t dir = SOC_INTEGRAL_DIR_NONE;

    if (magnitude_a10 != 0) *magnitude_a10 = 0u;
    if (!g_soc_input_valid) return dir;
    /* Unsigned subtraction handles INT32_MIN without signed overflow. */
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

uint8_t isCHG(void)
{
    return (soc_current_direction(0) == SOC_INTEGRAL_DIR_CHG) ? 1u : 0u;
}

uint8_t isDSG(void)
{
    return (soc_current_direction(0) == SOC_INTEGRAL_DIR_DSG) ? 1u : 0u;
}

uint8_t get_soc_real(void)
{
    return SOC_Calculate_Element.u8SOC_Now;
}

static uint8_t get_soc_display(void)
{
    return g_soc_display_soc;
}

void set_dispsoc(uint8_t soc)
{
    g_soc_display_soc = soc_limit_percent_u32(soc);
    g_soc_display_step_ticks = 0u;
    g_stCellInfoReport.SocElement.u16Soc = g_soc_display_soc;
}

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

static uint32_t soc_display_capacity_now(void)
{
    return ((uint32_t)get_soc_display() * SOC_Calculate_Element.u32CapFull) / SOC_PERCENT_MAX;
}

static uint32_t soc_nominal_capacity_0p1ah(void)
{
    bms_cold_system_params_t system;
    if (bms_cold_kv_store_get_system(&system) && system.capacity_factory > 0u &&
        system.capacity_factory <= BMS_SOC_CAPACITY_MAX_0P1AH) return system.capacity_factory;
    return (uint32_t)CapacityFactory;
}

static void soc_recalc_full_capacity(void)
{
    uint32_t factory = soc_nominal_capacity_0p1ah();
    SOC_Calculate_Element.u32CapFactory = factory * SOC_CAPACITY_UNITS_PER_FACTORY;

    if (g_soc_runtime.capacity_learned && g_soc_runtime.learned_capacity_0p1ah != 0u) {
        uint32_t learned = g_soc_runtime.learned_capacity_0p1ah;
        uint32_t soh = (factory == 0u) ? 100u : (learned * 100u) / factory;
        if (soh > 100u) soh = 100u;
        SOC_Calculate_Element.soh = (uint8_t)soh;
        SOC_Calculate_Element.u32CapFull = learned * SOC_CAPACITY_UNITS_PER_FACTORY;
        g_soc_runtime.soh_source = BMS_SOC_SOH_SOURCE_CAPACITY;
        g_soc_runtime.soh_confidence = 100u;
    } else {
        SOC_Calculate_Element.soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));
        SOC_Calculate_Element.u32CapFull =
            (SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh) / 100u;
        g_soc_runtime.soh_source = BMS_SOC_SOH_SOURCE_ESTIMATED_CYCLE;
        g_soc_runtime.soh_confidence = 25u;
    }

    if (SOC_Calculate_Element.u32CapFull == 0u) SOC_Calculate_Element.u32CapFull = 1u;
}

static void soc_recalc_now_capacity(void)
{
    SOC_Calculate_Element.u32CapNow =
        ((uint32_t)get_soc_real() * SOC_Calculate_Element.u32CapFull) / SOC_PERCENT_MAX;
}

static void soc_reset_integral_accumulator(void)
{
    SOC_Calculate_Element.u32CapChange = 0u;
    SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0u;
    SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0u;
    g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
    g_soc_integral_tick_remainder = 0u;
}

static void soc_integral_select_dir(soc_integral_dir_t dir)
{
    if (g_soc_integral_dir != dir) {
        g_soc_integral_dir = dir;
        g_soc_integral_tick_remainder = 0u;
        SOC_Calculate_Element.u32CapChange = 0u;
    }
}

static uint32_t soc_integral_delta_from_current(uint16_t current_a10, soc_integral_dir_t dir)
{
    uint32_t magnitude_ma;
    uint32_t ticks_left;
    uint32_t delta;
    uint32_t fractional_ma;
    const uint32_t denominator = BMS_SOC_TIME_TICKS_PER_SECOND * 100u;
    (void)current_a10; /* coarse current is retained only for legacy sag tables */
    if (!g_soc_input_valid || g_soc_interval_32k == 0u) return 0u;
    soc_integral_select_dir(dir);
    magnitude_ma = (g_soc_input_current_ma < 0) ?
        (0u - (uint32_t)g_soc_input_current_ma) : (uint32_t)g_soc_input_current_ma;
    /* The pinned TC32 linker has no 64-bit multiply/divide helpers. Split
     * magnitude into whole denominator units and bounded fractional chunks.
     * Whole result <= 671 * 12800; each sum < 3200000 * 1025 < UINT32_MAX.
     * At most 13 iterations for the accepted 400 ms interval, with exactly
     * the same quotient/remainder as mA * ticks / denominator. */
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

static uint8_t soc_percent_from_capacity_charge(uint32_t cap)
{
    if (cap >= SOC_Calculate_Element.u32CapFull) return SOC_PERCENT_MAX;
    return soc_limit_percent_u32((cap * SOC_PERCENT_MAX) / SOC_Calculate_Element.u32CapFull);
}

static uint8_t soc_percent_from_capacity_discharge(uint32_t cap)
{
    uint32_t percent;
    if (cap == 0u) return 0u;
    if (cap >= SOC_Calculate_Element.u32CapFull) return SOC_PERCENT_MAX;
    percent = ((cap * SOC_PERCENT_MAX) + SOC_Calculate_Element.u32CapFull - 1u) /
        SOC_Calculate_Element.u32CapFull;
    return soc_limit_percent_u32(percent);
}

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

static void soc_learning_abort(void)
{
    g_soc_runtime.learning_state = BMS_SOC_LEARNING_NONE;
    g_soc_runtime.learning_capacity_as10 = 0u;
}

static uint16_t soc_sat_inc_u16(uint16_t value)
{
    return (value == 65535u) ? value : (uint16_t)(value + 1u);
}

static uint32_t soc_learning_persist_flags(void)
{
    uint32_t flags = SOC_KV_FLAG_LEARNING_META |
        (soc_nominal_capacity_0p1ah() << SOC_KV_FLAG_NOMINAL_SHIFT);
    if (g_soc_runtime.capacity_learned) flags |= SOC_KV_FLAG_CAPACITY_LEARNED;
    if (g_soc_runtime.learning_state != BMS_SOC_LEARNING_NONE)
        flags |= SOC_KV_FLAG_LEARNING_ACTIVE;
    return flags;
}

static void soc_learning_update_confidence(void)
{
    uint16_t confidence;
    if (g_soc_runtime.capacity_learned) {
        g_soc_runtime.learning_confidence = 100u;
        return;
    }
    confidence = (uint16_t)g_soc_runtime.candidate_match_count *
        (100u / SOC_LEARNING_CONFIRM_CYCLES);
    if (confidence > 99u) confidence = 99u;
    g_soc_runtime.learning_confidence = (uint8_t)confidence;
}

static void soc_learning_persist(void)
{
    (void)soc_kv_store_write_learning_meta(
        (u32)g_soc_runtime.learned_capacity_0p1ah,
        soc_learning_persist_flags(),
        (u32)g_soc_runtime.candidate_capacity_0p1ah,
        (u32)g_soc_runtime.valid_learning_count,
        (u32)g_soc_runtime.rejected_learning_count,
        (u32)g_soc_runtime.last_learning_reject_reason,
        (u32)g_soc_runtime.candidate_match_count);
}

static void soc_learning_reject(uint8_t reason)
{
    if (g_soc_runtime.learning_state != BMS_SOC_LEARNING_NONE)
        g_soc_runtime.rejected_learning_count =
            soc_sat_inc_u16(g_soc_runtime.rejected_learning_count);
    g_soc_runtime.last_learning_reject_reason = reason;
    g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_LEARNING_REJECTED;
    soc_learning_abort();
    soc_learning_update_confidence();
    soc_learning_persist();
}

static void soc_learning_start(uint8_t state)
{
    int32_t offset_ma;
    uint32_t gain_ppm;
    if (!bms_config_get_current_calibration(&offset_ma, &gain_ppm)) {
        g_soc_runtime.last_learning_reject_reason =
            BMS_SOC_LEARNING_REJECT_CALIBRATION_CHANGED;
        soc_learning_abort();
        soc_learning_persist();
        return;
    }
    g_soc_runtime.learning_state = state;
    g_soc_runtime.learning_capacity_as10 = 0u;
    g_soc_runtime.learning_current_offset_ma = offset_ma;
    g_soc_runtime.learning_current_gain_ppm = gain_ppm;
    soc_learning_persist();
}

static void soc_learning_add(uint32_t delta)
{
    if ((0xFFFFFFFFu - g_soc_runtime.learning_capacity_as10) < delta)
        g_soc_runtime.learning_capacity_as10 = 0xFFFFFFFFu;
    else
        g_soc_runtime.learning_capacity_as10 += delta;
}

static uint8_t soc_learning_accept_candidate(void)
{
    uint32_t nominal = soc_nominal_capacity_0p1ah();
    uint32_t candidate = (g_soc_runtime.learning_capacity_as10 + 1800u) / 3600u;
    uint32_t min_cap = (nominal * SOC_LEARNED_CAP_MIN_PERCENT) / 100u;
    uint32_t max_cap = (nominal * SOC_LEARNED_CAP_MAX_PERCENT) / 100u;

    if ((nominal == 0u) || (candidate < min_cap) || (candidate > max_cap) ||
        (candidate > BMS_SOC_CAPACITY_MAX_0P1AH)) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_CAPACITY_RANGE);
        return 0u;
    }

    g_soc_runtime.valid_learning_count = soc_sat_inc_u16(g_soc_runtime.valid_learning_count);
    if (g_soc_runtime.candidate_match_count == 0u) {
        g_soc_runtime.candidate_capacity_0p1ah = (uint16_t)candidate;
        g_soc_runtime.candidate_match_count = 1u;
    } else {
        uint32_t tolerance;
        tolerance = ((uint32_t)g_soc_runtime.candidate_capacity_0p1ah *
                     SOC_LEARNING_CANDIDATE_TOLERANCE_PERCENT) / 100u;
        if (tolerance == 0u) tolerance = 1u;
        if (soc_abs_diff_u16((uint16_t)candidate,
                             g_soc_runtime.candidate_capacity_0p1ah) > tolerance) {
            g_soc_runtime.rejected_learning_count =
                soc_sat_inc_u16(g_soc_runtime.rejected_learning_count);
            g_soc_runtime.last_learning_reject_reason =
                BMS_SOC_LEARNING_REJECT_CANDIDATE_INCONSISTENT;
            g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_LEARNING_REJECTED;
            g_soc_runtime.candidate_capacity_0p1ah = (uint16_t)candidate;
            g_soc_runtime.candidate_match_count = 1u;
        } else {
            uint32_t averaged = ((uint32_t)g_soc_runtime.candidate_capacity_0p1ah +
                                 candidate + 1u) / 2u;
            g_soc_runtime.candidate_capacity_0p1ah = (uint16_t)averaged;
            if (g_soc_runtime.candidate_match_count < 255u)
                g_soc_runtime.candidate_match_count++;
        }
    }

    if (g_soc_runtime.candidate_match_count >= SOC_LEARNING_CONFIRM_CYCLES) {
        uint32_t base = g_soc_runtime.capacity_learned ?
            g_soc_runtime.learned_capacity_0p1ah : nominal;
        uint32_t max_step = (base * SOC_LEARNING_UPDATE_MAX_PERCENT) / 100u;
        uint32_t target = g_soc_runtime.candidate_capacity_0p1ah;
        if (max_step == 0u) max_step = 1u;
        if (target > base + max_step) target = base + max_step;
        else if (target + max_step < base) target = base - max_step;
        g_soc_runtime.learned_capacity_0p1ah = (uint16_t)target;
        g_soc_runtime.capacity_learned = 1u;
        g_soc_runtime.candidate_match_count = 0u;
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }

    soc_learning_abort();
    soc_learning_update_confidence();
    soc_learning_persist();
    return 1u;
}

static void soc_learning_on_delta(soc_integral_dir_t dir, uint32_t delta)
{
    if (!g_soc_config.capacity_learning_enable || delta == 0u) return;

    if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_EMPTY_TO_FULL) {
        if (dir == SOC_INTEGRAL_DIR_CHG) soc_learning_add(delta);
        else if (dir == SOC_INTEGRAL_DIR_DSG)
            soc_learning_reject(BMS_SOC_LEARNING_REJECT_DIRECTION_REVERSE);
    } else if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_FULL_TO_EMPTY) {
        if (dir == SOC_INTEGRAL_DIR_DSG) soc_learning_add(delta);
        else if (dir == SOC_INTEGRAL_DIR_CHG)
            soc_learning_reject(BMS_SOC_LEARNING_REJECT_DIRECTION_REVERSE);
    }
}

static void soc_learning_on_full_anchor(void)
{
    uint8_t quality;
    if (!g_soc_config.capacity_learning_enable) return;
    quality = soc_learning_full_quality();
    if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_EMPTY_TO_FULL) {
        if (quality) (void)soc_learning_accept_candidate();
        else soc_learning_reject(BMS_SOC_LEARNING_REJECT_LOW_QUALITY_FULL);
    }
    if (quality) soc_learning_start(BMS_SOC_LEARNING_FULL_TO_EMPTY);
    else soc_learning_abort();
}

static void soc_learning_on_empty_anchor(void)
{
    uint8_t quality;
    if (!g_soc_config.capacity_learning_enable) return;
    quality = soc_learning_empty_quality();
    if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_FULL_TO_EMPTY) {
        if (quality) (void)soc_learning_accept_candidate();
        else soc_learning_reject(BMS_SOC_LEARNING_REJECT_LOW_QUALITY_EMPTY);
    }
    if (quality) soc_learning_start(BMS_SOC_LEARNING_EMPTY_TO_FULL);
    else soc_learning_abort();
}

static void soc_apply_integral_delta(soc_integral_dir_t dir, uint32_t delta)
{
    uint8_t old_soc;
    uint8_t new_soc;
    old_soc = get_soc_real();
    g_soc_runtime.last_integral_delta_as10 = delta;
    soc_diag_note_action(BMS_SOC_ACTION_INTEGRATE, old_soc, old_soc, 0u);
    if (delta == 0u) return;

    SOC_Calculate_Element.u32CapChange += delta;
    soc_learning_on_delta(dir, delta);

    if (dir == SOC_INTEGRAL_DIR_CHG) {
        if ((SOC_Calculate_Element.u32CapNow >= SOC_Calculate_Element.u32CapFull) ||
            ((SOC_Calculate_Element.u32CapFull - SOC_Calculate_Element.u32CapNow) <= delta))
            SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
        else
            SOC_Calculate_Element.u32CapNow += delta;

        new_soc = soc_percent_from_capacity_charge(SOC_Calculate_Element.u32CapNow);
        if (new_soc > old_soc) {
            SOC_Calculate_Element.u8SOC_Now = new_soc;
            SOC_Calculate_Element.u32CapChange = 0u;
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
            SOC_Calculate_Element.u32CapChange = 0u;
            soc_note_discharge_soc_drop(old_soc, new_soc);
        }
    }
    g_soc_runtime.last_soc_after = get_soc_real();
}

static void soc_apply_real_value(uint8_t soc, uint8_t sync_display)
{
    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc);
    soc_recalc_now_capacity();
    if (sync_display) set_dispsoc(SOC_Calculate_Element.u8SOC_Now);
}

static uint8_t soc_step_down_to(uint8_t target_soc)
{
    uint8_t current = get_soc_real();
    target_soc = soc_limit_percent_u32(target_soc);
    if (current <= target_soc) return 0u;
    soc_apply_real_value((uint8_t)(current - 1u), 0u);
    soc_reset_integral_accumulator();
    return 1u;
}

static uint8_t soc_step_up_to(uint8_t target_soc)
{
    uint8_t current = get_soc_real();
    target_soc = soc_limit_percent_u32(target_soc);
    if (current >= target_soc) return 0u;
    soc_apply_real_value((uint8_t)(current + 1u), 0u);
    soc_reset_integral_accumulator();
    return 1u;
}

static uint16_t soc_weighted_cell_mv(void)
{
    return (uint16_t)((((uint32_t)VCELLMIN * 3u) + (uint32_t)VCELLMAX) / 4u);
}

static uint8_t soc_ocv_sample_valid(void)
{
    if (g_soc_profile == 0) return 0u;
    if ((VCELLMIN < g_soc_profile->valid_min_mv) ||
        (VCELLMAX > g_soc_profile->valid_max_mv) ||
        (VCELLMAX < VCELLMIN) ||
        (g_soc_input.cell_delta_mv > SOC_OCV_CELL_DELTA_MAX_MV)) return 0u;
    return 1u;
}

static uint8_t soc_temperature_reasonable(void)
{
    if (!g_soc_input.temperature_valid) return 0u;
    if ((g_soc_input.temperature_min_x10 < SOC_OCV_TEMP_MIN_X10) ||
        (g_soc_input.temperature_max_x10 > SOC_OCV_TEMP_MAX_X10) ||
        (g_soc_input.temperature_max_x10 < g_soc_input.temperature_min_x10)) return 0u;
    return 1u;
}

static uint8_t soc_rest_context_valid(void)
{
    if (!g_soc_input.voltage_valid || !soc_temperature_reasonable() ||
        g_soc_input.balancing_active || g_soc_input.heating_active ||
        g_soc_input.open_wire_active || g_soc_input.open_wire_suspected ||
        g_soc_input.afe_fault || g_soc_input.temperature_fault ||
        g_soc_input.current_fault || g_soc_input.pack_fault) return 0u;
    return 1u;
}

static uint8_t soc_idle_for_ocv(void)
{
    return (soc_current_direction(0) == SOC_INTEGRAL_DIR_NONE) ? 1u : 0u;
}

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

static uint32_t soc_ocv_prepare_ticks(void)
{
    return (uint32_t)g_soc_config.ocv_rest_prepare_s * SOC_TICKS_PER_SECOND;
}

static void soc_reset_ocv_tracking(void)
{
    g_soc_runtime.idle_stable_ticks = 0u;
    g_soc_runtime.ocv_down_ticks = 0u;
    g_soc_runtime.idle_ocv_mv_valid = 0u;
    g_soc_runtime.ocv_confidence = 0u;
    g_soc_runtime.ocv_state = BMS_SOC_OCV_WAIT_CURRENT;
    g_soc_runtime.ocv_mv = 0u;
    g_soc_runtime.ocv_center = 0u;
    g_soc_runtime.ocv_low = 0u;
    g_soc_runtime.ocv_high = 0u;
}

static uint8_t soc_idle_ocv_tracking(void)
{
    uint16_t mv;
    uint16_t diff;
    uint32_t prepare_ticks;
    uint32_t confidence;
    uint8_t current_soc;

    if (!soc_idle_for_ocv() || !soc_rest_context_valid() ||
        !soc_ocv_sample_valid() ||
        (g_soc_input.cell_delta_mv > SOC_OCV_IDLE_CELL_DELTA_MAX_MV)) {
        soc_reset_ocv_tracking();
        return 0u;
    }

    mv = soc_weighted_cell_mv();
    if (!g_soc_runtime.idle_ocv_mv_valid) {
        g_soc_runtime.idle_ocv_last_mv = mv;
        g_soc_runtime.idle_ocv_mv_valid = 1u;
        g_soc_runtime.ocv_state = BMS_SOC_OCV_PREPARE;
        return 0u;
    }

    diff = soc_abs_diff_u16(mv, g_soc_runtime.idle_ocv_last_mv);
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

    /* OCV is a confidence interval, not a hard target.  Normal OCV correction
     * is deliberately one-way: it may reduce an over-estimated SOC to the
     * upper confidence boundary, but it never raises SOC.  Only a confirmed
     * full-charge anchor may increase SOC toward 100%. */
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

static uint16_t soc_discharge_natural_1pct_ticks(uint16_t dsg_current)
{
    uint16_t factory_a10;
    uint32_t ticks;
    if (dsg_current < SOC_DSG_CURRENT_MIN_A10) dsg_current = SOC_DSG_CURRENT_MIN_A10;
    factory_a10 = (uint16_t)soc_nominal_capacity_0p1ah();
    /* CapacityFactory is Ah*10, current is A*10: 1% time(s) = 36 * CapacityFactory / current. */
    ticks = ((uint32_t)36u * factory_a10 * SOC_TICKS_PER_SECOND + ((uint32_t)dsg_current / 2u)) /
        (uint32_t)dsg_current;
    if (ticks < SOC_DSG_NATURAL_STEP_MIN_TICKS) ticks = SOC_DSG_NATURAL_STEP_MIN_TICKS;
    if (ticks > SOC_DSG_NATURAL_STEP_MAX_TICKS) ticks = SOC_DSG_NATURAL_STEP_MAX_TICKS;
    return (uint16_t)ticks;
}

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

static uint8_t soc_discharge_sag_hold_active(void)
{
    return (g_soc_runtime.dsg_sag_hold_ticks > 0u) ? 1u : 0u;
}

static uint16_t soc_uvp_trip_mv(void)
{
    uint16_t uvp = g_tParam.protect.u16VcellUvp_Third;
    if ((uvp < 2200u) || (uvp > 3500u)) uvp = g_soc_profile->empty_sync_mv;
    return uvp;
}

static uint32_t soc_abs_i32(int32_t value)
{
    return (value < 0) ? (0u - (uint32_t)value) : (uint32_t)value;
}

static uint32_t soc_learning_endpoint_current_max_ma(void)
{
    return (soc_nominal_capacity_0p1ah() *
            g_soc_profile->learning_endpoint_max_c_rate_x1000) / 10u;
}

static uint8_t soc_learning_common_quality(soc_integral_dir_t required_dir)
{
    if (!g_soc_input_valid || !soc_ocv_sample_valid() ||
        !soc_temperature_reasonable() ||
        g_soc_input.cell_delta_mv > g_soc_profile->learning_cell_delta_max_mv ||
        g_soc_input.afe_fault || g_soc_input.open_wire_active ||
        g_soc_input.open_wire_suspected || g_soc_input.balancing_active ||
        g_soc_input.heating_active || g_soc_input.temperature_fault ||
        g_soc_input.current_fault || g_soc_input.pack_fault ||
        soc_current_direction(0) != required_dir ||
        soc_abs_i32(g_soc_input_current_ma) > soc_learning_endpoint_current_max_ma())
        return 0u;
    return 1u;
}

static uint8_t soc_learning_full_quality(void)
{
    uint16_t full_min = (g_soc_profile->full_sync_mv > g_soc_profile->full_min_margin_mv) ?
        (uint16_t)(g_soc_profile->full_sync_mv - g_soc_profile->full_min_margin_mv) : 0u;
    return (soc_learning_common_quality(SOC_INTEGRAL_DIR_CHG) &&
            VCELLMAX >= g_soc_profile->full_sync_mv && VCELLMIN >= full_min &&
            g_soc_input.cell_delta_mv <= g_soc_profile->full_cell_delta_max_mv) ? 1u : 0u;
}

static uint8_t soc_learning_empty_quality(void)
{
    uint16_t empty_limit = (uint16_t)(soc_uvp_trip_mv() +
                                      g_soc_profile->terminal_l3_offset_mv);
    return (soc_learning_common_quality(SOC_INTEGRAL_DIR_DSG) &&
            VCELLMIN <= empty_limit && !soc_discharge_sag_hold_active()) ? 1u : 0u;
}

static void soc_learning_monitor_quality(void)
{
    int32_t offset_ma;
    uint32_t gain_ppm;
    if (!g_soc_config.capacity_learning_enable ||
        g_soc_runtime.learning_state == BMS_SOC_LEARNING_NONE) return;
    if (g_soc_input.afe_fault) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_AFE_COMMUNICATION); return;
    }
    if (!bms_config_get_current_calibration(&offset_ma, &gain_ppm) ||
        offset_ma != g_soc_runtime.learning_current_offset_ma ||
        gain_ppm != g_soc_runtime.learning_current_gain_ppm) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_CALIBRATION_CHANGED); return;
    }
    if (g_soc_input.open_wire_active || g_soc_input.open_wire_suspected) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_OPEN_WIRE); return;
    }
    if (g_soc_input.balancing_active) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_BALANCING); return;
    }
    if (g_soc_input.heating_active) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_HEATING); return;
    }
    if (!soc_temperature_reasonable() || g_soc_input.temperature_fault) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_TEMPERATURE); return;
    }
    if (g_soc_input.cell_delta_mv > g_soc_profile->learning_cell_delta_max_mv) {
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_CELL_IMBALANCE); return;
    }
    if (g_soc_input.current_fault || g_soc_input.pack_fault)
        soc_learning_reject(BMS_SOC_LEARNING_REJECT_PROTECTION);
}

static void soc_eta_reset(void)
{
    g_soc_runtime.eta_filtered_current_ma = 0;
    g_soc_runtime.eta_variation_ma = 0u;
    g_soc_runtime.eta_peak_current_ma = 0u;
    g_soc_runtime.eta_stable_ticks = 0u;
    g_soc_runtime.time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
    g_soc_runtime.time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
    g_soc_runtime.eta_state = BMS_SOC_ETA_INVALID;
    g_soc_runtime.eta_direction = BMS_SOC_ETA_DIR_NONE;
    g_soc_runtime.eta_confidence = 0u;
    g_soc_runtime.eta_valid = 0u;
}

static int32_t soc_eta_filter_step(int32_t filtered, int32_t sample)
{
    if (sample > filtered) {
        uint32_t difference = (uint32_t)sample - (uint32_t)filtered;
        return filtered + (int32_t)(difference >> SOC_ETA_FILTER_SHIFT);
    }
    if (sample < filtered) {
        uint32_t difference = (uint32_t)filtered - (uint32_t)sample;
        return filtered - (int32_t)(difference >> SOC_ETA_FILTER_SHIFT);
    }
    return filtered;
}

static uint16_t soc_eta_minutes(uint32_t capacity_as10, uint32_t current_ma)
{
    uint32_t numerator;
    uint32_t denominator;
    uint32_t remainder;
    uint32_t minutes;
    if (current_ma == 0u) return BMS_SOC_ETA_MINUTES_INVALID;
    /* minutes = capacity_as10 * 100 / current_ma / 60.  Reduce to 5/3
     * before multiplying so the maximum supported capacity stays within
     * 32 bits and no TC32 64-bit runtime helper is introduced. */
    if (capacity_as10 > (0xFFFFFFFFu / 5u))
        return BMS_SOC_ETA_MINUTES_INVALID - 1u;
    if (current_ma > (0xFFFFFFFFu / 3u)) return 0u;
    numerator = capacity_as10 * 5u;
    denominator = current_ma * 3u;
    minutes = numerator / denominator;
    remainder = numerator % denominator;
    if (remainder >= ((denominator / 2u) + (denominator & 1u))) minutes++;
    if (minutes >= BMS_SOC_ETA_MINUTES_INVALID) minutes = BMS_SOC_ETA_MINUTES_INVALID - 1u;
    return (uint16_t)minutes;
}

static void soc_eta_update(void)
{
    soc_integral_dir_t dir = soc_current_direction(0);
    uint8_t eta_dir;
    uint32_t magnitude;
    uint32_t deviation;
    uint32_t variation_limit;
    uint32_t confidence_drop;

    if (dir == SOC_INTEGRAL_DIR_NONE) {
        soc_eta_reset(); return;
    }
    eta_dir = (dir == SOC_INTEGRAL_DIR_CHG) ? BMS_SOC_ETA_DIR_CHARGE :
        BMS_SOC_ETA_DIR_DISCHARGE;
    if (g_soc_runtime.eta_direction != eta_dir) {
        soc_eta_reset();
        g_soc_runtime.eta_direction = eta_dir;
        g_soc_runtime.eta_filtered_current_ma = g_soc_input_current_ma;
        g_soc_runtime.eta_peak_current_ma = soc_abs_i32(g_soc_input_current_ma);
        g_soc_runtime.eta_stable_ticks = 1u;
        g_soc_runtime.eta_state = BMS_SOC_ETA_STABILIZING;
        return;
    }

    g_soc_runtime.eta_filtered_current_ma =
        soc_eta_filter_step(g_soc_runtime.eta_filtered_current_ma, g_soc_input_current_ma);
    deviation = (g_soc_input_current_ma >= g_soc_runtime.eta_filtered_current_ma) ?
        ((uint32_t)g_soc_input_current_ma -
         (uint32_t)g_soc_runtime.eta_filtered_current_ma) :
        ((uint32_t)g_soc_runtime.eta_filtered_current_ma -
         (uint32_t)g_soc_input_current_ma);
    if (deviation > g_soc_runtime.eta_variation_ma)
        g_soc_runtime.eta_variation_ma +=
            (deviation - g_soc_runtime.eta_variation_ma) >> SOC_ETA_FILTER_SHIFT;
    else
        g_soc_runtime.eta_variation_ma -=
            (g_soc_runtime.eta_variation_ma - deviation) >> SOC_ETA_FILTER_SHIFT;
    magnitude = soc_abs_i32(g_soc_runtime.eta_filtered_current_ma);
    if (magnitude > g_soc_runtime.eta_peak_current_ma)
        g_soc_runtime.eta_peak_current_ma = magnitude;
    if (g_soc_runtime.eta_stable_ticks < 65535u) g_soc_runtime.eta_stable_ticks++;
    if (g_soc_runtime.eta_stable_ticks < SOC_ETA_STABLE_TICKS) {
        g_soc_runtime.eta_state = BMS_SOC_ETA_STABILIZING; return;
    }

    variation_limit = (magnitude * SOC_ETA_VARIATION_PERCENT) / 100u;
    if (variation_limit < SOC_ETA_VARIATION_MIN_MA)
        variation_limit = SOC_ETA_VARIATION_MIN_MA;
    if (magnitude <= BMS_CURRENT_UNRELIABLE_MAX_MA ||
        magnitude <= g_soc_config.current_deadband_ma ||
        g_soc_runtime.eta_variation_ma > variation_limit ||
        g_soc_runtime.endpoint_state != BMS_SOC_ENDPOINT_NORMAL) {
        g_soc_runtime.eta_state = BMS_SOC_ETA_LOW_CONFIDENCE;
        g_soc_runtime.eta_valid = 0u;
        g_soc_runtime.eta_confidence = 20u;
        g_soc_runtime.time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
        g_soc_runtime.time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
        return;
    }

    if (eta_dir == BMS_SOC_ETA_DIR_CHARGE &&
        VCELLMAX + g_soc_profile->full_min_margin_mv >= g_soc_profile->full_sync_mv &&
        g_soc_runtime.eta_peak_current_ma > 0u &&
        magnitude < (g_soc_runtime.eta_peak_current_ma / 100u) * SOC_ETA_TAPER_PERCENT) {
        g_soc_runtime.eta_state = BMS_SOC_ETA_LOW_CONFIDENCE;
        g_soc_runtime.eta_valid = 0u;
        g_soc_runtime.eta_confidence = 10u;
        g_soc_runtime.time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
        g_soc_runtime.time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
        return;
    }

    confidence_drop = g_soc_runtime.eta_variation_ma / (magnitude / 100u);
    if (confidence_drop > 80u) confidence_drop = 80u;
    g_soc_runtime.eta_confidence = (uint8_t)(100u - confidence_drop);
    g_soc_runtime.eta_state = BMS_SOC_ETA_VALID;
    g_soc_runtime.eta_valid = 1u;
    g_soc_runtime.time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
    g_soc_runtime.time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
    if (eta_dir == BMS_SOC_ETA_DIR_DISCHARGE)
        g_soc_runtime.time_to_empty_min =
            soc_eta_minutes(SOC_Calculate_Element.u32CapNow, magnitude);
    else
        g_soc_runtime.time_to_full_min =
            soc_eta_minutes(SOC_Calculate_Element.u32CapFull -
                            SOC_Calculate_Element.u32CapNow, magnitude);
}

static uint8_t soc_terminal_lookup(uint8_t *target_soc, uint8_t *sag_hold_blocks)
{
    uint16_t uvp = soc_uvp_trip_mv();
    if ((target_soc == 0) || (sag_hold_blocks == 0) || (VCELLMAX < VCELLMIN)) return 0u;

    if (VCELLMIN <= uvp) {
        /* Raw voltage at/below UVP is not itself the protection decision.
         * A high-current sag remains held here; the independently filtered
         * Third Cell UVP path above still forces the final safety anchor. */
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
                soc_learning_on_empty_anchor();
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

static uint8_t soc_apply_full_anchor(void)
{
    /* Upward calibration is legal only while a real charging direction is
     * confirmed. Idle/high-voltage boot states and rebound must never raise SOC. */
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
            soc_learning_on_full_anchor();
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
            soc_learning_on_full_anchor();
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
            soc_learning_on_full_anchor();
        }
        if (get_soc_real() == SOC_PERCENT_MAX)
            g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_FULL;
        soc_diag_note_action(BMS_SOC_ACTION_FULL_ANCHOR, before,
                             SOC_PERCENT_MAX, 2u);
        return 1u;
    }
    return 0u;
}

static uint8_t soc_apply_forced_empty_anchor(void)
{
    uint8_t before;
    if (!g_soc_input.third_cell_uvp) return 0u;
    before = get_soc_real();
    if (before > 5u) g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_EARLY_UVP;
    if (before > 10u) g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_CAPACITY_MISMATCH;
    if (soc_discharge_sag_hold_active())
        g_soc_runtime.endpoint_event_flags |= SOC_ENDPOINT_EVENT_LARGE_SAG;
    if (g_soc_input.cell_delta_mv > g_soc_profile->learning_cell_delta_max_mv)
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
        soc_learning_on_empty_anchor();
    }
    g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
    return 1u;
}

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
            soc_learning_on_empty_anchor();
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
            soc_learning_on_empty_anchor();
        }
        if (get_soc_real() == 0u)
            g_soc_runtime.endpoint_state = BMS_SOC_ENDPOINT_CONFIRMED_EMPTY;
        soc_diag_note_action(BMS_SOC_ACTION_IDLE_EMPTY, before, 0u, 0u);
        return 1u;
    }
    return 0u;
}

static uint16_t soc_fault_filter_samples(void)
{
    uint32_t ms = (uint32_t)g_tParam.protect.u16SocUp_Filter * 10u;
    uint32_t samples = (ms + SOC_INTEGRAL_PERIOD_MS - 1u) / SOC_INTEGRAL_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

static union MDLCHGFAULT_REG *soc_fault_reg(uint8_t level)
{
    if (level == 0u) return &g_stCellInfoReport.unMdlFault_First;
    if (level == 1u) return &g_stCellInfoReport.unMdlFault_Second;
    return &g_stCellInfoReport.unMdlFault_Third;
}

static uint16_t soc_fault_threshold(uint8_t level)
{
    if (level == 0u) return g_tParam.protect.u16SocUp_First;
    if (level == 1u) return g_tParam.protect.u16SocUp_Second;
    return g_tParam.protect.u16SocUp_Third;
}

static bms_fault_code_t soc_fault_history_code(uint8_t level)
{
    /* Legacy enum says SOC_HIGH, but protocol bit b1SocLow and parameter values
     * are low-SOC thresholds. Keep numeric protocol compatibility. */
    if (level == 0u) return BMS_FAULT_SOC_HIGH_FIRST;
    if (level == 1u) return BMS_FAULT_SOC_HIGH_SECOND;
    return BMS_FAULT_SOC_HIGH_THIRD;
}

static void soc_update_low_faults(void)
{
    uint8_t level;
    uint8_t soc = get_soc_real();
    uint16_t needed = soc_fault_filter_samples();

    for (level = 0u; level < 3u; ++level) {
        uint16_t trip = soc_fault_threshold(level);
        uint16_t recover = g_tParam.protect.u16SocUp_Rcv;

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

static void soc_strategy_update(void)
{
    soc_profile_refresh();
    /* Keep CONFIRMED_FULL active long enough for display SOC to finish its
     * soft landing even if charge current disappears immediately afterward. */
    g_soc_runtime.endpoint_state =
        (g_soc_runtime.full_anchor_latched && get_soc_real() == SOC_PERCENT_MAX &&
         get_soc_display() < SOC_PERCENT_MAX) ?
        BMS_SOC_ENDPOINT_CONFIRMED_FULL : BMS_SOC_ENDPOINT_NORMAL;
    soc_update_discharge_sag_hold();

    if (soc_apply_full_anchor()) { soc_eta_update(); return; }
    if (soc_apply_forced_empty_anchor()) { soc_eta_update(); return; }
    soc_learning_monitor_quality();
    if (soc_apply_discharge_terminal_tracking()) { soc_eta_update(); return; }
    if (soc_apply_idle_empty_anchor()) { soc_eta_update(); return; }
    (void)soc_idle_ocv_tracking();
    soc_eta_update();
}

void set_calsoc(uint8_t soc)
{
    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc);
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
}

void set_soc_param(uint8_t soc, uint16_t cap_factory, uint8_t sync_display)
{
    uint8_t before = get_soc_real();
    (void)cap_factory;
    set_calsoc(soc);
    soc_invalidate_sample_interval();
    soc_reset_integral_accumulator();
    soc_reset_ocv_tracking();
    if (sync_display) set_dispsoc(get_soc_real());
    soc_recalc_now_capacity();
    soc_diag_note_action(BMS_SOC_ACTION_PARAMETER_SET, before,
                         soc_limit_percent_u32(soc), sync_display);
}

void soc_param_lib_init(const soc_kv_data_t *soc)
{
    soc_kv_data_t defaults;
    uint8_t learning_meta_changed = 0u;
    memset(&g_soc_runtime, 0, sizeof(g_soc_runtime));
    soc_invalidate_sample_interval();
    soc_load_persisted_product_config();
    soc_profile_refresh();

    if (soc == 0) {
        defaults = soc_kv_store_get_default_data();
        soc = &defaults;
    }

    SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(soc->dsg);
    SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(soc->cycle);
    SOC_Calculate_Element.u32CapFull_Cal_As = 0u;
    if ((soc->flags >> SOC_KV_FLAG_NOMINAL_SHIFT) == soc_nominal_capacity_0p1ah()) {
        if ((soc->flags & SOC_KV_FLAG_CAPACITY_LEARNED) &&
            soc->learned_capacity_0p1ah != 0u) {
            g_soc_runtime.capacity_learned = 1u;
            g_soc_runtime.learned_capacity_0p1ah =
                (soc->learned_capacity_0p1ah > 65535u) ?
                65535u : (uint16_t)soc->learned_capacity_0p1ah;
        }
        if (soc->flags & SOC_KV_FLAG_LEARNING_META) {
            g_soc_runtime.candidate_capacity_0p1ah =
                (uint16_t)soc->candidate_capacity_0p1ah;
            g_soc_runtime.valid_learning_count = (uint16_t)soc->valid_learning_count;
            g_soc_runtime.rejected_learning_count = (uint16_t)soc->rejected_learning_count;
            g_soc_runtime.last_learning_reject_reason =
                (uint8_t)soc->last_learning_reject_reason;
            g_soc_runtime.candidate_match_count = (uint8_t)soc->candidate_match_count;
        }
        if (soc->flags & SOC_KV_FLAG_LEARNING_ACTIVE) {
            g_soc_runtime.rejected_learning_count =
                soc_sat_inc_u16(g_soc_runtime.rejected_learning_count);
            g_soc_runtime.last_learning_reject_reason =
                BMS_SOC_LEARNING_REJECT_REBOOT;
            learning_meta_changed = 1u;
        }
    }

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
    soc_learning_update_confidence();
    g_soc_initialized = 1u;
    if (learning_meta_changed) soc_learning_persist();
    SOC_Result_Pass();
}

void SOC_Cont_AH_Int_CHG(void)
{
    uint16_t current = 0u;
    uint32_t delta;
    if (soc_current_direction(&current) != SOC_INTEGRAL_DIR_CHG) {
        SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0u;
        return;
    }

    SOC_Cali_Flag = SOC_CALI_CONT_CHG;
    SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 1u;
    SOC_Calculate_Element.u8SOC_Old = get_soc_real();
    delta = soc_integral_delta_from_current(current, SOC_INTEGRAL_DIR_CHG);
    soc_apply_integral_delta(SOC_INTEGRAL_DIR_CHG, delta);
    SOC_Calculate_Element.u32CapFull_Cal_As += delta;
    SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0u;
}

void SOC_Cont_AH_Int_DSG(void)
{
    uint16_t current = 0u;
    uint32_t delta;
    if (soc_current_direction(&current) != SOC_INTEGRAL_DIR_DSG) {
        SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0u;
        return;
    }

    SOC_Cali_Flag = SOC_CALI_CONT_DSG;
    SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 1u;
    SOC_Calculate_Element.u8SOC_Old = get_soc_real();
    delta = soc_integral_delta_from_current(current, SOC_INTEGRAL_DIR_DSG);
    soc_apply_integral_delta(SOC_INTEGRAL_DIR_DSG, delta);
    SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0u;
}

void SOC_State_Transfer(void)
{
    SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
    soc_reset_integral_accumulator();
}

void SOC_Result_Pass(void)
{
    uint8_t hide_capacity;
    soc_display_follow_real();
    g_stCellInfoReport.SocElement.u16Soc = get_soc_display();
    g_stCellInfoReport.SocElement.u16Soh = SOC_Calculate_Element.soh;
    g_stCellInfoReport.SocElement.u16Cycle_times = soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times);

    hide_capacity = (g_soc_config.capacity_learning_enable &&
                     g_soc_config.hide_capacity_until_learned &&
                     !g_soc_runtime.capacity_learned) ? 1u : 0u;
    if (hide_capacity) {
        g_stCellInfoReport.SocElement.u16CapacityNow = 0u;
        g_stCellInfoReport.SocElement.u16CapacityFull = 0u;
        g_stCellInfoReport.SocElement.u16CapacityFactory = 0u;
    } else {
        g_stCellInfoReport.SocElement.u16CapacityNow =
            (uint16_t)(soc_display_capacity_now() / SOC_REPORT_CAPACITY_DIVISOR);
        g_stCellInfoReport.SocElement.u16CapacityFull =
            (uint16_t)(SOC_Calculate_Element.u32CapFull / SOC_REPORT_CAPACITY_DIVISOR);
        g_stCellInfoReport.SocElement.u16CapacityFactory =
            (uint16_t)(SOC_Calculate_Element.u32CapFactory / SOC_REPORT_CAPACITY_DIVISOR);
    }
}

static void soc_invalidate_sample_interval(void)
{
    g_soc_input_valid = 0u;
    g_soc_input_ready = 0u;
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
    soc_learning_abort();
}

static uint8_t soc_external_state_changed(const bms_soc_sample_t *sample,
                                          uint8_t *reason)
{
    uint8_t changed = 0u;
    *reason = BMS_SOC_LEARNING_REJECT_NONE;
    if (sample->charger_state_known) {
        if (g_soc_charger_state_ready &&
            g_soc_last_charger_present != sample->charger_present) {
            changed = 1u;
            *reason = BMS_SOC_LEARNING_REJECT_CHARGER_CHANGE;
        }
        g_soc_last_charger_present = sample->charger_present;
        g_soc_charger_state_ready = 1u;
    }
    if (sample->load_state_known) {
        if (g_soc_load_state_ready &&
            g_soc_last_load_present != sample->load_present) {
            changed = 1u;
            if (*reason == BMS_SOC_LEARNING_REJECT_NONE)
                *reason = BMS_SOC_LEARNING_REJECT_LOAD_CHANGE;
        }
        g_soc_last_load_present = sample->load_present;
        g_soc_load_state_ready = 1u;
    }
    return changed;
}

void bms_soc_process_sample(const bms_soc_sample_t *sample)
{
    uint32_t elapsed_32k;
    const uint32_t quantum_32k = BMS_SOC_TIME_TICKS_PER_SECOND / SOC_TICKS_PER_SECOND;
    soc_integral_dir_t dir;
    soc_integral_dir_t previous_dir;
    uint8_t external_change;
    uint8_t learning_reject_reason;

    if (sample == 0) {
        soc_diag_note_sample(BMS_SOC_SAMPLE_INVALID, SOC_INTEGRAL_DIR_NONE, 0u);
        soc_invalidate_sample_interval();
        return;
    }
    g_soc_input = *sample;
    external_change = soc_external_state_changed(sample, &learning_reject_reason);

    if (!g_soc_initialized || !sample->sample_valid || !sample->voltage_valid)
    {
        soc_diag_note_sample(BMS_SOC_SAMPLE_INVALID, SOC_INTEGRAL_DIR_NONE, 0u);
        if (g_soc_initialized &&
            (!sample->sample_valid || !sample->voltage_valid) &&
            g_soc_runtime.learning_state != BMS_SOC_LEARNING_NONE)
            soc_learning_reject(BMS_SOC_LEARNING_REJECT_INVALID_SAMPLE);
        soc_invalidate_sample_interval();
        return;
    }
    if (!g_soc_input_ready)
    {
        soc_diag_note_sample(BMS_SOC_SAMPLE_FIRST,
                             SOC_INTEGRAL_DIR_NONE, 0u);
        g_soc_sample_tick_32k = sample->timestamp_32k;
        g_soc_input_current_ma = sample->current_ma;
        g_soc_input_valid = 1u;
        g_soc_input_ready = 1u;
        return; /* the first new sample cannot prove the preceding interval */
    }
    elapsed_32k = sample->timestamp_32k - g_soc_sample_tick_32k;
    if (elapsed_32k == 0u) {
        soc_diag_note_sample(BMS_SOC_SAMPLE_DUPLICATE,
                             soc_current_direction(0), 0u);
        return; /* duplicate cached read is not new evidence */
    }
    g_soc_sample_tick_32k = sample->timestamp_32k;
    if (elapsed_32k > BMS_SOC_MAX_SAMPLE_GAP_32K)
    {
        soc_diag_note_sample(BMS_SOC_SAMPLE_GAP,
                             SOC_INTEGRAL_DIR_NONE, elapsed_32k);
        if (g_soc_runtime.learning_state != BMS_SOC_LEARNING_NONE)
            soc_learning_reject(BMS_SOC_LEARNING_REJECT_SAMPLE_GAP);
        soc_invalidate_sample_interval();
        /* Current frame starts a new interval; never fill a blind gap. */
        g_soc_sample_tick_32k = sample->timestamp_32k;
        g_soc_input_current_ma = sample->current_ma;
        g_soc_input_valid = 1u;
        g_soc_input_ready = 1u;
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
        if (g_soc_runtime.learning_state != BMS_SOC_LEARNING_NONE)
            soc_learning_reject(learning_reject_reason);
    }
    if (dir == SOC_INTEGRAL_DIR_CHG) SOC_Cont_AH_Int_CHG();
    else if (dir == SOC_INTEGRAL_DIR_DSG) SOC_Cont_AH_Int_DSG();
    else SOC_State_Transfer();

    if (dir != previous_dir)
    {
        g_soc_runtime.last_sample_state = BMS_SOC_SAMPLE_DIRECTION_CHANGE;
        /* An interval straddling a current-state transition proves neither
         * continuous rest nor a continuous full/empty anchor condition. */
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

    /* Existing calibration thresholds stay in 200 ms quanta, but credit comes
     * from measured time. At most two quanta per new bounded-gap sample.
     * The 8 mV slope check is kept conservative at longer intervals. */
    g_soc_strategy_pending_32k += elapsed_32k;
    while (g_soc_strategy_pending_32k >= quantum_32k)
    {
        g_soc_strategy_pending_32k -= quantum_32k;
        soc_strategy_update();
        soc_update_low_faults();
        SOC_Result_Pass();
    }
    g_soc_interval_32k = 0u; /* cannot integrate this sample twice through legacy APIs */
}

void APP_SOC_IntEnhance_Ctrl(uint8_t valid, int32_t current_ma,
                             uint32_t sample_tick_32k)
{
    bms_afe_feature_snapshot_t feature;
    bms_soc_sample_t sample;
    uint16_t third_faults = g_stCellInfoReport.unMdlFault_Third.all;
    memset(&sample, 0, sizeof(sample));
    memset(&feature, 0, sizeof(feature));

    sample.timestamp_32k = sample_tick_32k;
    sample.current_ma = current_ma;
    sample.pack_voltage_mv = (uint32_t)g_stCellInfoReport.u16VCellTotle * 10u;
    sample.cell_min_mv = g_stCellInfoReport.u16VCellMin;
    sample.cell_max_mv = g_stCellInfoReport.u16VCellMax;
    sample.cell_delta_mv = g_stCellInfoReport.u16VCellDelta;
    sample.sample_valid = valid ? 1u : 0u;
    sample.voltage_valid = (valid && sample.cell_min_mv != 0u &&
                            sample.cell_max_mv >= sample.cell_min_mv) ? 1u : 0u;
    if (valid && bms_afe_get_feature_snapshot(&feature) &&
        feature.valid && feature.battery_temp_valid) {
        sample.temperature_valid = 1u;
        sample.temperature_min_x10 = feature.battery_temp_min_x10;
        sample.temperature_max_x10 = feature.battery_temp_max_x10;
    }
    sample.balancing_active = bms_features_balance_active();
    sample.heating_active = bms_features_heater_on();
    sample.open_wire_active = bms_features_openwire_active();
    sample.open_wire_suspected = bms_features_openwire_suspected();
    sample.afe_fault = bms_error_get(BMS_ERROR_AFE1);
    sample.temperature_fault =
        ((third_faults & SOC_LEARNING_TEMP_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.current_fault =
        ((third_faults & SOC_LEARNING_CURRENT_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.pack_fault =
        ((third_faults & SOC_LEARNING_PACK_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.third_cell_ovp =
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp;
    sample.third_cell_uvp =
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp;
    sample.charger_state_known = 1u;
    sample.charger_present = bms_features_charge_session_active();
    bms_soc_process_sample(&sample);
}

void bms_soc_nominal_capacity_changed(void)
{
    g_soc_runtime.capacity_learned = 0u;
    g_soc_runtime.learned_capacity_0p1ah = 0u;
    g_soc_runtime.candidate_capacity_0p1ah = 0u;
    g_soc_runtime.candidate_match_count = 0u;
    g_soc_runtime.valid_learning_count = 0u;
    g_soc_runtime.rejected_learning_count = 0u;
    g_soc_runtime.last_learning_reject_reason = BMS_SOC_LEARNING_REJECT_NONE;
    g_soc_runtime.learning_confidence = 0u;
    soc_learning_abort();
    (void)soc_kv_store_write_learning_meta(0u, 0u, 0u, 0u, 0u, 0u, 0u);
    soc_recalc_full_capacity();
    set_soc_param(get_soc_real(), 0u, 1u);
    SOC_Result_Pass();
}
