#include "SocEnhance.h"
#include "bms_config_store.h"
#include "bms_soc_profile.h"
#include "bms_cold_kv_store.h"
#include "bms_state.h"
#include "param.h"
#include <string.h>

#define VCELLMAX g_stCellInfoReport.u16VCellMax
#define VCELLMIN g_stCellInfoReport.u16VCellMin
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

#ifndef BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT
#define BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u
#endif
#ifndef BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT
#define BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT 1u
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
} soc_runtime_t;

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;
static soc_cali_state_t SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
static soc_runtime_t g_soc_runtime;
static soc_integral_dir_t g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
/* mA * 32k-ticks remainder, denominator 100 mA per As*10 unit. */
static uint32_t g_soc_integral_tick_remainder;
static int32_t g_soc_input_current_ma;
static uint32_t g_soc_sample_tick_32k;
static uint32_t g_soc_interval_32k;
static uint32_t g_soc_strategy_pending_32k;
static uint8_t g_soc_input_valid;
static uint8_t g_soc_input_ready;
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
    if (g_soc_display_soc == real_soc) {
        g_soc_display_step_ticks = 0u;
        return;
    }

    if (g_soc_display_step_ticks < SOC_DISPLAY_STEP_TICKS) g_soc_display_step_ticks++;
    if (g_soc_display_step_ticks < SOC_DISPLAY_STEP_TICKS) return;
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
    } else {
        SOC_Calculate_Element.soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));
        SOC_Calculate_Element.u32CapFull =
            (SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh) / 100u;
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

static void soc_learning_add(uint32_t delta)
{
    if ((0xFFFFFFFFu - g_soc_runtime.learning_capacity_as10) < delta)
        g_soc_runtime.learning_capacity_as10 = 0xFFFFFFFFu;
    else
        g_soc_runtime.learning_capacity_as10 += delta;
}

static void soc_learning_accept(void)
{
    uint32_t nominal = soc_nominal_capacity_0p1ah();
    uint32_t learned = (g_soc_runtime.learning_capacity_as10 + 1800u) / 3600u;
    uint32_t min_cap = (nominal * SOC_LEARNED_CAP_MIN_PERCENT) / 100u;
    uint32_t max_cap = (nominal * SOC_LEARNED_CAP_MAX_PERCENT) / 100u;

    if ((nominal == 0u) || (learned < min_cap) || (learned > max_cap) || (learned > BMS_SOC_CAPACITY_MAX_0P1AH)) {
        soc_learning_abort();
        return;
    }

    g_soc_runtime.learned_capacity_0p1ah = (uint16_t)learned;
    g_soc_runtime.capacity_learned = 1u;
    (void)soc_kv_store_write_learning((u32)g_soc_runtime.learned_capacity_0p1ah,
                                      SOC_KV_FLAG_CAPACITY_LEARNED | (soc_nominal_capacity_0p1ah() << 16));
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
    soc_learning_abort();
}

static void soc_learning_on_delta(soc_integral_dir_t dir, uint32_t delta)
{
    if (!g_soc_config.capacity_learning_enable || delta == 0u) return;

    if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_EMPTY_TO_FULL) {
        if (dir == SOC_INTEGRAL_DIR_CHG) soc_learning_add(delta);
        else if (dir == SOC_INTEGRAL_DIR_DSG) soc_learning_abort();
    } else if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_FULL_TO_EMPTY) {
        if (dir == SOC_INTEGRAL_DIR_DSG) soc_learning_add(delta);
        else if (dir == SOC_INTEGRAL_DIR_CHG) soc_learning_abort();
    }
}

static void soc_learning_on_full_anchor(void)
{
    if (!g_soc_config.capacity_learning_enable) return;
    if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_EMPTY_TO_FULL)
        soc_learning_accept();
    g_soc_runtime.learning_state = BMS_SOC_LEARNING_FULL_TO_EMPTY;
    g_soc_runtime.learning_capacity_as10 = 0u;
}

static void soc_learning_on_empty_anchor(void)
{
    if (!g_soc_config.capacity_learning_enable) return;
    if (g_soc_runtime.learning_state == BMS_SOC_LEARNING_FULL_TO_EMPTY)
        soc_learning_accept();
    g_soc_runtime.learning_state = BMS_SOC_LEARNING_EMPTY_TO_FULL;
    g_soc_runtime.learning_capacity_as10 = 0u;
}

static void soc_apply_integral_delta(soc_integral_dir_t dir, uint32_t delta)
{
    uint8_t old_soc;
    uint8_t new_soc;
    if (delta == 0u) return;

    old_soc = get_soc_real();
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
        (g_stCellInfoReport.u16VCellDelta > SOC_OCV_CELL_DELTA_MAX_MV)) return 0u;
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

    if (!soc_idle_for_ocv() || !soc_ocv_sample_valid() ||
        (g_stCellInfoReport.u16VCellDelta > SOC_OCV_IDLE_CELL_DELTA_MAX_MV)) {
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
    return soc_step_down_to(g_soc_runtime.ocv_high);
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

static uint8_t soc_terminal_lookup(uint8_t *target_soc, uint8_t *sag_hold_blocks)
{
    uint16_t uvp = soc_uvp_trip_mv();
    if ((target_soc == 0) || (sag_hold_blocks == 0) || (VCELLMAX < VCELLMIN)) return 0u;

    if (VCELLMIN <= uvp) {
        *target_soc = 0u; *sag_hold_blocks = 0u; return 1u;
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
            if (!g_soc_runtime.empty_anchor_latched) {
                g_soc_runtime.empty_anchor_latched = 1u;
                soc_learning_on_empty_anchor();
            }
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
    return soc_step_down_to(target_soc);
}

static uint8_t soc_apply_full_anchor(void)
{
    /* Upward calibration is legal only while a real charging direction is
     * confirmed. Idle/high-voltage boot states and rebound must never raise SOC. */
    uint16_t full_mv = g_soc_profile->full_sync_mv;
    uint16_t full_min = (full_mv > g_soc_profile->full_min_margin_mv) ?
        (uint16_t)(full_mv - g_soc_profile->full_min_margin_mv) : 0u;
    uint8_t voltage_ready = (VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && isCHG();

    if (isCHG() && g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp) {
        if (get_soc_real() != SOC_PERCENT_MAX) {
            soc_apply_real_value(SOC_PERCENT_MAX, 0u);
            soc_reset_integral_accumulator();
        }
        if (!g_soc_runtime.full_anchor_latched) {
            g_soc_runtime.full_anchor_latched = 1u;
            g_soc_runtime.empty_anchor_latched = 0u;
            soc_learning_on_full_anchor();
        }
        return 1u;
    }

    if (!voltage_ready) {
        g_soc_runtime.full_lock_ticks = 0u;
        g_soc_runtime.full_adjust_ticks = 0u;
        if (VCELLMAX + 100u < full_mv) g_soc_runtime.full_anchor_latched = 0u;
        return 0u;
    }

    if (g_soc_runtime.full_lock_ticks < SOC_FULL_LOCK_TICKS) g_soc_runtime.full_lock_ticks++;
    if (g_soc_runtime.full_lock_ticks < SOC_FULL_LOCK_TICKS) return 0u;

    if (get_soc_real() >= SOC_PERCENT_MAX) {
        if (!g_soc_runtime.full_anchor_latched) {
            g_soc_runtime.full_anchor_latched = 1u;
            g_soc_runtime.empty_anchor_latched = 0u;
            soc_learning_on_full_anchor();
        }
        return 0u;
    }

    if (g_soc_runtime.full_adjust_ticks < SOC_FULL_SYNC_STEP_TICKS)
        g_soc_runtime.full_adjust_ticks++;
    if (g_soc_runtime.full_adjust_ticks < SOC_FULL_SYNC_STEP_TICKS) return 0u;
    g_soc_runtime.full_adjust_ticks = 0u;
    if (soc_step_up_to(SOC_PERCENT_MAX)) {
        if (get_soc_real() == SOC_PERCENT_MAX && !g_soc_runtime.full_anchor_latched) {
            g_soc_runtime.full_anchor_latched = 1u;
            g_soc_runtime.empty_anchor_latched = 0u;
            soc_learning_on_full_anchor();
        }
        return 1u;
    }
    return 0u;
}

static uint8_t soc_apply_forced_empty_anchor(void)
{
    if (!g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp) return 0u;
    if (get_soc_real() != 0u) {
        soc_apply_real_value(0u, 1u);
        soc_reset_integral_accumulator();
    }
    if (!g_soc_runtime.empty_anchor_latched) {
        g_soc_runtime.empty_anchor_latched = 1u;
        g_soc_runtime.full_anchor_latched = 0u;
        soc_learning_on_empty_anchor();
    }
    return 1u;
}

static uint8_t soc_apply_idle_empty_anchor(void)
{
    uint16_t empty_mv = g_soc_profile->empty_sync_mv;
    uint16_t empty_max = (uint16_t)(empty_mv + g_soc_profile->empty_max_margin_mv);
    if (!soc_idle_for_ocv() || (VCELLMIN > empty_mv) || (VCELLMAX > empty_max)) {
        g_soc_runtime.empty_lock_ticks = 0u;
        g_soc_runtime.empty_adjust_ticks = 0u;
        if (VCELLMIN > empty_mv + 100u) g_soc_runtime.empty_anchor_latched = 0u;
        return 0u;
    }

    if (g_soc_runtime.empty_lock_ticks < SOC_EMPTY_LOCK_TICKS) g_soc_runtime.empty_lock_ticks++;
    if (g_soc_runtime.empty_lock_ticks < SOC_EMPTY_LOCK_TICKS) return 0u;
    if (get_soc_real() == 0u) {
        if (!g_soc_runtime.empty_anchor_latched) {
            g_soc_runtime.empty_anchor_latched = 1u;
            g_soc_runtime.full_anchor_latched = 0u;
            soc_learning_on_empty_anchor();
        }
        return 0u;
    }

    if (g_soc_runtime.empty_adjust_ticks < SOC_EMPTY_SYNC_STEP_TICKS)
        g_soc_runtime.empty_adjust_ticks++;
    if (g_soc_runtime.empty_adjust_ticks < SOC_EMPTY_SYNC_STEP_TICKS) return 0u;
    g_soc_runtime.empty_adjust_ticks = 0u;
    if (soc_step_down_to(0u)) {
        if (get_soc_real() == 0u && !g_soc_runtime.empty_anchor_latched) {
            g_soc_runtime.empty_anchor_latched = 1u;
            g_soc_runtime.full_anchor_latched = 0u;
            soc_learning_on_empty_anchor();
        }
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
        union MDLCHGFAULT_REG *fault = soc_fault_reg(level);

        if (trip == 0u || trip > SOC_PERCENT_MAX) {
            g_soc_runtime.soc_low_active[level] = 0u;
            g_soc_runtime.soc_low_trip_count[level] = 0u;
            g_soc_runtime.soc_low_recover_count[level] = 0u;
            fault->bits.b1SocLow = 0u;
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
        fault->bits.b1SocLow = g_soc_runtime.soc_low_active[level];
    }
}

static void soc_strategy_update(void)
{
    soc_profile_refresh();
    soc_update_discharge_sag_hold();

    if (soc_apply_full_anchor()) return;
    if (soc_apply_forced_empty_anchor()) return;
    if (soc_apply_discharge_terminal_tracking()) return;
    if (soc_apply_idle_empty_anchor()) return;
    (void)soc_idle_ocv_tracking();
}

void set_calsoc(uint8_t soc)
{
    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc);
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
}

void set_soc_param(uint8_t soc, uint16_t cap_factory, uint8_t sync_display)
{
    (void)cap_factory;
    set_calsoc(soc);
    soc_invalidate_sample_interval();
    soc_reset_integral_accumulator();
    soc_reset_ocv_tracking();
    if (sync_display) set_dispsoc(get_soc_real());
    soc_recalc_now_capacity();
}

void soc_param_lib_init(const soc_kv_data_t *soc)
{
    soc_kv_data_t defaults;
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
    if ((soc->flags & SOC_KV_FLAG_CAPACITY_LEARNED) &&
        (soc->flags >> 16) == soc_nominal_capacity_0p1ah() && soc->learned_capacity_0p1ah != 0u) {
        g_soc_runtime.capacity_learned = 1u;
        g_soc_runtime.learned_capacity_0p1ah =
            (soc->learned_capacity_0p1ah > 65535u) ? 65535u : (uint16_t)soc->learned_capacity_0p1ah;
    }

    SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc->soc);
    soc_recalc_full_capacity();
    soc_recalc_now_capacity();
    set_dispsoc(get_soc_real());
    soc_reset_integral_accumulator();
    soc_reset_ocv_tracking();
    g_soc_initialized = 1u;
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
    g_soc_display_step_ticks = 0u;
    memset(g_soc_runtime.soc_low_trip_count, 0, sizeof(g_soc_runtime.soc_low_trip_count));
    memset(g_soc_runtime.soc_low_recover_count, 0, sizeof(g_soc_runtime.soc_low_recover_count));
    soc_learning_abort();
}

void APP_SOC_IntEnhance_Ctrl(uint8_t valid, int32_t current_ma, uint32_t sample_tick_32k)
{
    uint32_t elapsed_32k;
    const uint32_t quantum_32k = BMS_SOC_TIME_TICKS_PER_SECOND / SOC_TICKS_PER_SECOND;
    soc_integral_dir_t dir;
    soc_integral_dir_t previous_dir;

    if (!g_soc_initialized || !valid)
    {
        soc_invalidate_sample_interval();
        return;
    }
    if (!g_soc_input_ready)
    {
        g_soc_sample_tick_32k = sample_tick_32k;
        g_soc_input_current_ma = current_ma;
        g_soc_input_valid = 1u;
        g_soc_input_ready = 1u;
        return; /* the first new sample cannot prove the preceding interval */
    }
    elapsed_32k = sample_tick_32k - g_soc_sample_tick_32k;
    if (elapsed_32k == 0u) return; /* duplicate cached read is not new evidence */
    g_soc_sample_tick_32k = sample_tick_32k;
    if (elapsed_32k > BMS_SOC_MAX_SAMPLE_GAP_32K)
    {
        soc_invalidate_sample_interval();
        /* Current frame starts a new interval; never fill a blind gap. */
        g_soc_sample_tick_32k = sample_tick_32k;
        g_soc_input_current_ma = current_ma;
        g_soc_input_valid = 1u;
        g_soc_input_ready = 1u;
        return;
    }
    previous_dir = soc_current_direction(0);
    g_soc_input_current_ma = current_ma;
    g_soc_input_valid = 1u;
    g_soc_interval_32k = elapsed_32k;
    dir = soc_current_direction(0);
    if (dir == SOC_INTEGRAL_DIR_CHG) SOC_Cont_AH_Int_CHG();
    else if (dir == SOC_INTEGRAL_DIR_DSG) SOC_Cont_AH_Int_DSG();
    else SOC_State_Transfer();

    if (dir != previous_dir)
    {
        /* An interval straddling a current-state transition proves neither
         * continuous rest nor a continuous full/empty anchor condition. */
        soc_reset_ocv_tracking();
        g_soc_runtime.full_lock_ticks = 0u;
        g_soc_runtime.full_adjust_ticks = 0u;
        g_soc_runtime.empty_lock_ticks = 0u;
        g_soc_runtime.empty_adjust_ticks = 0u;
        g_soc_runtime.dsg_empty_lock_ticks = 0u;
        g_soc_runtime.dsg_terminal_adjust_ticks = 0u;
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

void bms_soc_nominal_capacity_changed(void)
{
    g_soc_runtime.capacity_learned = 0u;
    g_soc_runtime.learned_capacity_0p1ah = 0u;
    (void)soc_kv_store_write_learning(0u, 0u);
    soc_recalc_full_capacity();
    set_soc_param(get_soc_real(), 0u, 1u);
    SOC_Result_Pass();
}
