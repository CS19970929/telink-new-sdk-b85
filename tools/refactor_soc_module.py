#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

SOC_H = r'''#ifndef SOCENHANCE_H
#define SOCENHANCE_H

#include "conf.h"
#include "soc_kv_store.h"

#define BMS_SOC_CHEMISTRY_AUTO 0u
#define BMS_SOC_CHEMISTRY_LFP  1u
#define BMS_SOC_CHEMISTRY_NMC  2u

#define BMS_SOC_OCV_WAIT_CURRENT   0u
#define BMS_SOC_OCV_PREPARE        1u
#define BMS_SOC_OCV_READY          2u
#define BMS_SOC_OCV_CORRECT_DOWN   3u

#define BMS_SOC_LEARNING_NONE          0u
#define BMS_SOC_LEARNING_EMPTY_TO_FULL 1u
#define BMS_SOC_LEARNING_FULL_TO_EMPTY 2u

typedef struct
{
    uint8_t chemistry;                  /* AUTO/LFP/NMC */
    uint16_t current_deadband_ma;       /* currents below this value are ignored */
    uint16_t ocv_rest_prepare_s;        /* stable idle time before OCV may correct */
    uint8_t ocv_error_band_percent;     /* +/- percentage points around OCV center */
    uint8_t capacity_learning_enable;   /* default disabled */
    uint8_t hide_capacity_until_learned;/* active only when learning is enabled */
} bms_soc_config_t;

typedef struct
{
    uint8_t chemistry;
    uint8_t soc_estimate;
    uint8_t soc_display;
    uint8_t ocv_state;
    uint8_t ocv_center;
    uint8_t ocv_low;
    uint8_t ocv_high;
    uint8_t ocv_confidence;
    uint8_t capacity_learned;
    uint8_t learning_state;
    uint16_t ocv_cell_mv;
    uint16_t rest_seconds;
    uint16_t learned_capacity_0p1ah;
} bms_soc_diag_t;

struct SOC_CALCULATE_ELEMENT
{
    UINT32 u32CapFactory;         /* As*10 */
    UINT32 u32CapChange;          /* As*10 since the last integer SOC step */
    uint8_t u8CHG_AHCalcu_Flag;
    uint8_t u8DSG_AHCalcu_Flag;
    uint8_t u8SOC_Now;            /* estimated SOC, 0..100 */
    UINT32 u32CapNow;             /* As*10 */
    uint8_t u8DSG_SOC_Int;        /* equivalent-discharge percent accumulator */
    UINT32 u32Cycle_times;
    UINT32 u32CapFull;            /* As*10 */
    uint8_t u8SOC_Old;
    UINT32 u32CapFull_Cal_As;
    uint8_t soh;
};

extern struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;

void bms_soc_get_default_config(bms_soc_config_t *config);
uint8_t bms_soc_configure(const bms_soc_config_t *config);
uint8_t bms_soc_get_chemistry(void);
void bms_soc_get_diag(bms_soc_diag_t *diag);
void bms_soc_refresh_profile_from_params(void);

void APP_SOC_IntEnhance_Ctrl(void);
void SOC_Result_Pass(void);
void SOC_Cont_AH_Int_CHG(void);
void SOC_Cont_AH_Int_DSG(void);
void SOC_State_Transfer(void);
void set_soc_param(uint8_t soc, uint16_t cap_factory, uint8_t sync_display);
void set_calsoc(uint8_t soc);
void set_dispsoc(uint8_t soc);
uint8_t get_soc_real(void);
uint8_t isCHG(void);
uint8_t isDSG(void);
void soc_param_lib_init(const soc_kv_data_t *soc);
uint8_t bms_soh_from_cycle(uint16_t cycle);

#endif /* SOCENHANCE_H */
'''

SOC_C = r'''#include "SocEnhance.h"
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

typedef struct
{
    uint16_t mv;
    uint8_t soc;
} soc_ocv_point_t;

typedef struct
{
    uint8_t chemistry;
    const soc_ocv_point_t *ocv;
    uint8_t ocv_count;
    uint16_t valid_min_mv;
    uint16_t valid_max_mv;
    uint16_t full_sync_mv;
    uint16_t full_min_margin_mv;
    uint16_t empty_sync_mv;
    uint16_t empty_max_margin_mv;
    uint16_t terminal_start_offset_mv;
    uint16_t terminal_l1_offset_mv;
    uint16_t terminal_l2_offset_mv;
    uint16_t terminal_l3_offset_mv;
} soc_profile_t;

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

static const soc_ocv_point_t g_soc_ocv_lfp[] = {
    {2800u, 0u}, {3000u, 2u}, {3100u, 5u}, {3200u, 10u},
    {3250u, 15u}, {3280u, 25u}, {3300u, 35u}, {3315u, 45u},
    {3330u, 55u}, {3340u, 65u}, {3350u, 75u}, {3370u, 85u},
    {3400u, 95u}, {3450u, 98u}, {3500u, 100u},
};

static const soc_ocv_point_t g_soc_ocv_nmc[] = {
    {3000u, 0u}, {3300u, 5u}, {3450u, 10u}, {3550u, 20u},
    {3650u, 30u}, {3700u, 40u}, {3750u, 50u}, {3800u, 60u},
    {3850u, 70u}, {3900u, 80u}, {4000u, 90u}, {4100u, 96u},
    {4180u, 100u},
};

static const soc_profile_t g_soc_profile_lfp = {
    BMS_SOC_CHEMISTRY_LFP,
    g_soc_ocv_lfp,
    (uint8_t)(sizeof(g_soc_ocv_lfp) / sizeof(g_soc_ocv_lfp[0])),
    2500u, 3800u,
    3500u, 100u,
    3000u, 150u,
    150u, 100u, 50u, 20u,
};

static const soc_profile_t g_soc_profile_nmc = {
    BMS_SOC_CHEMISTRY_NMC,
    g_soc_ocv_nmc,
    (uint8_t)(sizeof(g_soc_ocv_nmc) / sizeof(g_soc_ocv_nmc[0])),
    2600u, 4300u,
    4180u, 200u,
    3000u, 200u,
    300u, 200u, 150u, 50u,
};

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;
static soc_cali_state_t SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
static soc_runtime_t g_soc_runtime;
static soc_integral_dir_t g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
static uint16_t g_soc_integral_ms_remainder;
static uint8_t g_soc_display_soc = (uint8_t)SOC_PARAM_DEFAULT_SOC;
static uint8_t g_soc_display_step_ticks;
static uint8_t g_soc_initialized;
static bms_soc_config_t g_soc_config = {
    BMS_SOC_CHEMISTRY_AUTO,
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
    config->current_deadband_ma = SOC_CURRENT_DEADBAND_MA_DEFAULT;
    config->ocv_rest_prepare_s = SOC_OCV_REST_PREPARE_SECONDS;
    config->ocv_error_band_percent = SOC_OCV_ERROR_BAND_PERCENT;
    config->capacity_learning_enable = BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT;
    config->hide_capacity_until_learned = BMS_SOC_HIDE_CAPACITY_UNTIL_LEARNED_DEFAULT;
}

uint8_t bms_soc_configure(const bms_soc_config_t *config)
{
    if (config == 0) return 0u;
    if (config->chemistry > BMS_SOC_CHEMISTRY_NMC) return 0u;
    if ((config->current_deadband_ma > 2000u) ||
        (config->ocv_rest_prepare_s < 60u) ||
        (config->ocv_rest_prepare_s > 3600u) ||
        (config->ocv_error_band_percent == 0u) ||
        (config->ocv_error_band_percent > 20u) ||
        (config->capacity_learning_enable > 1u) ||
        (config->hide_capacity_until_learned > 1u)) return 0u;

    g_soc_config = *config;
    soc_profile_refresh();
    soc_reset_ocv_tracking();
    if (g_soc_initialized) {
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }
    return 1u;
}

static uint8_t soc_resolve_chemistry(void)
{
    uint16_t ovp;
    if (g_soc_config.chemistry == BMS_SOC_CHEMISTRY_LFP ||
        g_soc_config.chemistry == BMS_SOC_CHEMISTRY_NMC) {
        return g_soc_config.chemistry;
    }

    ovp = g_tParam.protect.u16VcellOvp_Third;
    if ((ovp >= 3300u) && (ovp <= SOC_AUTO_LFP_OVP_MAX_MV)) return BMS_SOC_CHEMISTRY_LFP;
    if ((ovp > SOC_AUTO_LFP_OVP_MAX_MV) && (ovp <= 4500u)) return BMS_SOC_CHEMISTRY_NMC;

    /* Invalid/unconfigured protection data: preserve legacy NMC behavior, but
     * OCV validity checks still prevent out-of-range samples from correcting. */
    return BMS_SOC_CHEMISTRY_NMC;
}

static void soc_profile_refresh(void)
{
    uint8_t chemistry = soc_resolve_chemistry();
    const soc_profile_t *next = (chemistry == BMS_SOC_CHEMISTRY_LFP) ?
        &g_soc_profile_lfp : &g_soc_profile_nmc;
    if (g_soc_profile != next) {
        g_soc_profile = next;
        soc_reset_ocv_tracking();
    }
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
    diag->soc_estimate = SOC_Calculate_Element.u8SOC_Now;
    diag->soc_display = g_soc_display_soc;
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

static uint16_t soc_current_deadband_a10(void)
{
    uint32_t a10 = ((uint32_t)g_soc_config.current_deadband_ma + 99u) / 100u;
    if (a10 > 65535u) a10 = 65535u;
    return (uint16_t)a10;
}

static soc_integral_dir_t soc_current_direction(uint16_t *magnitude_a10)
{
    uint16_t deadband = soc_current_deadband_a10();
    uint16_t chg = ICHG;
    uint16_t dsg = IDSG;
    uint16_t diff;

    if (chg < deadband) chg = 0u;
    if (dsg < deadband) dsg = 0u;

    if (chg > dsg) {
        diff = (uint16_t)(chg - dsg);
        if (diff >= deadband) {
            if (magnitude_a10 != 0) *magnitude_a10 = diff;
            return SOC_INTEGRAL_DIR_CHG;
        }
    } else if (dsg > chg) {
        diff = (uint16_t)(dsg - chg);
        if (diff >= deadband) {
            if (magnitude_a10 != 0) *magnitude_a10 = diff;
            return SOC_INTEGRAL_DIR_DSG;
        }
    }

    if (magnitude_a10 != 0) *magnitude_a10 = 0u;
    return SOC_INTEGRAL_DIR_NONE;
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

static void soc_recalc_full_capacity(void)
{
    uint32_t factory = (uint32_t)CapacityFactory;
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
    g_soc_integral_ms_remainder = 0u;
}

static void soc_integral_select_dir(soc_integral_dir_t dir)
{
    if (g_soc_integral_dir != dir) {
        g_soc_integral_dir = dir;
        g_soc_integral_ms_remainder = 0u;
        SOC_Calculate_Element.u32CapChange = 0u;
    }
}

static uint32_t soc_integral_delta_from_current(uint16_t current_a10, soc_integral_dir_t dir)
{
    uint32_t sum;
    if (current_a10 == 0u) return 0u;
    soc_integral_select_dir(dir);
    sum = ((uint32_t)current_a10 * SOC_INTEGRAL_PERIOD_MS) + g_soc_integral_ms_remainder;
    g_soc_integral_ms_remainder = (uint16_t)(sum % SOC_INTEGRAL_MS_PER_SEC);
    return sum / SOC_INTEGRAL_MS_PER_SEC;
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
    uint32_t nominal = (uint32_t)CapacityFactory;
    uint32_t learned = (g_soc_runtime.learning_capacity_as10 + 1800u) / 3600u;
    uint32_t min_cap = (nominal * SOC_LEARNED_CAP_MIN_PERCENT) / 100u;
    uint32_t max_cap = (nominal * SOC_LEARNED_CAP_MAX_PERCENT) / 100u;

    if ((nominal == 0u) || (learned < min_cap) || (learned > max_cap) || (learned > 65535u)) {
        soc_learning_abort();
        return;
    }

    g_soc_runtime.learned_capacity_0p1ah = (uint16_t)learned;
    g_soc_runtime.capacity_learned = 1u;
    (void)soc_kv_store_write_learning((u32)g_soc_runtime.learned_capacity_0p1ah,
                                      SOC_KV_FLAG_CAPACITY_LEARNED);
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
    factory_a10 = (uint16_t)CapacityFactory;
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
    uint16_t full_mv = g_soc_profile->full_sync_mv;
    uint16_t full_min = (full_mv > g_soc_profile->full_min_margin_mv) ?
        (uint16_t)(full_mv - g_soc_profile->full_min_margin_mv) : 0u;
    uint8_t voltage_ready = (VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && !isDSG();

    if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp) {
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
    soc_reset_integral_accumulator();
    soc_reset_ocv_tracking();
    if (sync_display) set_dispsoc(get_soc_real());
    soc_recalc_now_capacity();
}

void soc_param_lib_init(const soc_kv_data_t *soc)
{
    soc_kv_data_t defaults;
    memset(&g_soc_runtime, 0, sizeof(g_soc_runtime));
    soc_profile_refresh();

    if (soc == 0) {
        defaults = soc_kv_store_get_default_data();
        soc = &defaults;
    }

    SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(soc->dsg);
    SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(soc->cycle);
    SOC_Calculate_Element.u32CapFull_Cal_As = 0u;
    if ((soc->flags & SOC_KV_FLAG_CAPACITY_LEARNED) && soc->learned_capacity_0p1ah != 0u) {
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

void APP_SOC_IntEnhance_Ctrl(void)
{
    soc_integral_dir_t dir = soc_current_direction(0);
    if (dir == SOC_INTEGRAL_DIR_CHG) SOC_Cont_AH_Int_CHG();
    else if (dir == SOC_INTEGRAL_DIR_DSG) SOC_Cont_AH_Int_DSG();
    else SOC_State_Transfer();

    soc_strategy_update();
    soc_update_low_faults();
    SOC_Result_Pass();
}
'''

SOC_KV_H = r'''#pragma once

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOC_KV_HOT_SECTOR_SIZE
#define SOC_KV_HOT_SECTOR_SIZE   FLASH_SECTOR_SIZE
#endif
#ifndef SOC_KV_HOT_SECTORS
#define SOC_KV_HOT_SECTORS   FLASH_ADDR_RUN_KV_SECTORS
#endif

#ifndef SOC_PARAM_DEFAULT_SOC
#define SOC_PARAM_DEFAULT_SOC    ((u32)FAC_INIT_soc)
#endif
#ifndef SOC_PARAM_DEFAULT_DSG
#define SOC_PARAM_DEFAULT_DSG    0u
#endif
#ifndef SOC_PARAM_DEFAULT_CYCLE
#define SOC_PARAM_DEFAULT_CYCLE  0u
#endif
#ifndef SOC_PARAM_DEFAULT_LEARNED_CAPACITY
#define SOC_PARAM_DEFAULT_LEARNED_CAPACITY 0u
#endif
#ifndef SOC_PARAM_DEFAULT_FLAGS
#define SOC_PARAM_DEFAULT_FLAGS 0u
#endif

#ifndef SOC_KV_DEFAULT_SOC
#define SOC_KV_DEFAULT_SOC    SOC_PARAM_DEFAULT_SOC
#endif
#ifndef SOC_KV_DEFAULT_DSG
#define SOC_KV_DEFAULT_DSG    SOC_PARAM_DEFAULT_DSG
#endif
#ifndef SOC_KV_DEFAULT_CYCLE
#define SOC_KV_DEFAULT_CYCLE  SOC_PARAM_DEFAULT_CYCLE
#endif

#define SOC_KV_FLAG_CAPACITY_LEARNED 0x00000001u

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
    u32 learned_capacity_0p1ah;
    u32 flags;
} soc_kv_data_t;

typedef struct {
    u32 active_base;
    u32 write_off;
    u32 next_seq;
    u32 active_generation;
    u16 active_sector;
    u8  loaded;
    u8  tail_dirty;
} soc_kv_dbg_t;

int  soc_kv_store_init(void);
soc_kv_data_t soc_kv_store_get_default_data(void);
soc_kv_data_t soc_kv_store_get(void);
int  soc_kv_store_write_all(u32 soc, u32 dsg, u32 cycle);
int  soc_kv_store_write_learning(u32 learned_capacity_0p1ah, u32 flags);
void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);

#ifdef __cplusplus
}
#endif
'''

SOC_KV_C = r'''#include "soc_kv_store.h"
#include "flash_kv32.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include <string.h>

#define SOC_KV_KEY_SOC               0x0001u
#define SOC_KV_KEY_DSG               0x0002u
#define SOC_KV_KEY_CYCLE             0x0003u
#define SOC_KV_KEY_LEARNED_CAPACITY  0x0004u
#define SOC_KV_KEY_FLAGS             0x0005u

static flash_kv32_t g_soc_kv;
static flash_kv32_cache_entry_t g_soc_cache[5];
static u32 g_soc_sector_addrs[(SOC_KV_HOT_SECTORS > 0) ? SOC_KV_HOT_SECTORS : 1];

static const flash_kv32_key_def_t g_soc_keys[] = {
    { SOC_KV_KEY_SOC,              SOC_KV_DEFAULT_SOC },
    { SOC_KV_KEY_DSG,              SOC_KV_DEFAULT_DSG },
    { SOC_KV_KEY_CYCLE,            SOC_KV_DEFAULT_CYCLE },
    { SOC_KV_KEY_LEARNED_CAPACITY, SOC_PARAM_DEFAULT_LEARNED_CAPACITY },
    { SOC_KV_KEY_FLAGS,            SOC_PARAM_DEFAULT_FLAGS },
};

static void soc_flash_read(void *ctx, u32 addr, u8 *buf, u32 len)
{
    (void)ctx;
    flash_read_page(addr, (int)len, buf);
}

static int soc_flash_prog(void *ctx, u32 addr, const u8 *buf, u32 len)
{
    (void)ctx;
    return flash_store_prog_checked(addr, buf, len);
}

static int soc_flash_erase_sector(void *ctx, u32 addr, u32 size)
{
    (void)ctx;
    return flash_store_erase_sector_checked(addr, size);
}

static const flash_kv32_port_t *soc_kv_port(void)
{
    static const flash_kv32_port_t port = { 0, soc_flash_read, soc_flash_prog, soc_flash_erase_sector, 0, 0 };
    return &port;
}

static void soc_kv_fill_sector_addrs(void)
{
    u16 i;
    u32 base = flash_store_cfg_get_soc_kv_base();
    for (i = 0; i < flash_store_cfg_get_soc_kv_sectors(); ++i)
        g_soc_sector_addrs[i] = base + ((u32)i * SOC_KV_HOT_SECTOR_SIZE);
}

static u32 soc_kv_get_value(u32 key, u32 default_value)
{
    u32 value = default_value;
    (void)flash_kv32_get(&g_soc_kv, key, &value);
    return value;
}

int soc_kv_store_init(void)
{
    flash_kv32_cfg_t cfg;
    if (flash_store_cfg_get_soc_kv_base() == 0u) return 0;
    soc_kv_fill_sector_addrs();
    memset(&cfg, 0, sizeof(cfg));
    cfg.port = soc_kv_port();
    cfg.sector_addrs = g_soc_sector_addrs;
    cfg.keys = g_soc_keys;
    cfg.sector_count = flash_store_cfg_get_soc_kv_sectors();
    cfg.sector_size = SOC_KV_HOT_SECTOR_SIZE;
    cfg.write_align = 4u;
    cfg.key_count = (u16)(sizeof(g_soc_keys) / sizeof(g_soc_keys[0]));
    if (!flash_kv32_init(&g_soc_kv, &cfg, g_soc_cache)) return 0;
    return 1;
}

soc_kv_data_t soc_kv_store_get_default_data(void)
{
    soc_kv_data_t data;
    data.soc = SOC_PARAM_DEFAULT_SOC;
    data.dsg = SOC_PARAM_DEFAULT_DSG;
    data.cycle = SOC_PARAM_DEFAULT_CYCLE;
    data.learned_capacity_0p1ah = SOC_PARAM_DEFAULT_LEARNED_CAPACITY;
    data.flags = SOC_PARAM_DEFAULT_FLAGS;
    return data;
}

soc_kv_data_t soc_kv_store_get(void)
{
    soc_kv_data_t data = soc_kv_store_get_default_data();
    data.soc = soc_kv_get_value(SOC_KV_KEY_SOC, data.soc);
    data.dsg = soc_kv_get_value(SOC_KV_KEY_DSG, data.dsg);
    data.cycle = soc_kv_get_value(SOC_KV_KEY_CYCLE, data.cycle);
    data.learned_capacity_0p1ah = soc_kv_get_value(SOC_KV_KEY_LEARNED_CAPACITY, data.learned_capacity_0p1ah);
    data.flags = soc_kv_get_value(SOC_KV_KEY_FLAGS, data.flags);
    return data;
}

int soc_kv_store_write_all(u32 soc, u32 dsg, u32 cycle)
{
    flash_kv32_pair_t pairs[3];
    if ((g_soc_kv.cfg.port == NULL) && !soc_kv_store_init()) return 0;
    pairs[0].key = SOC_KV_KEY_SOC; pairs[0].value = soc;
    pairs[1].key = SOC_KV_KEY_DSG; pairs[1].value = dsg;
    pairs[2].key = SOC_KV_KEY_CYCLE; pairs[2].value = cycle;
    return flash_kv32_write_pairs(&g_soc_kv, pairs, 3u) ? 1 : 0;
}

int soc_kv_store_write_learning(u32 learned_capacity_0p1ah, u32 flags)
{
    flash_kv32_pair_t pairs[2];
    if ((g_soc_kv.cfg.port == NULL) && !soc_kv_store_init()) return 0;
    pairs[0].key = SOC_KV_KEY_LEARNED_CAPACITY; pairs[0].value = learned_capacity_0p1ah;
    pairs[1].key = SOC_KV_KEY_FLAGS; pairs[1].value = flags;
    return flash_kv32_write_pairs(&g_soc_kv, pairs, 2u) ? 1 : 0;
}

void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle)
{
    soc_kv_data_t current;
    if (g_soc_kv.cfg.port == NULL) return;
    current = soc_kv_store_get();
    if ((current.soc == soc) && (current.dsg == dsg) && (current.cycle == cycle)) return;
    (void)soc_kv_store_write_all(soc, dsg, cycle);
}
'''

SOC_DOC = r'''# SOC 模块（当前实现）

本文件记录 `SocEnhance.c/.h` 与 `soc_kv_store.c/.h` 的真实行为。SOC 算法仍保留旧文件名以维持工程兼容，但内部已经按可复用 BMS SOC 模块整理。

## 1. 核心模型

- `SOC estimate`：库仑积分主线，固定 200 ms 积分周期。
- 电流报告单位为 0.1 A；默认 **< 200 mA 不积分**，视为静置候选。
- `SOC display`：与 estimate 分离，每 1 s 最多变化 1%，避免对外跳变。
- Flash 只保存 estimate SOC / 等效放电百分比 / cycle；显示 SOC 不保存。
- OCV 只用于长期纠偏，不作为运行中的主 SOC。

## 2. 三元 / 铁锂兼容

模块内置两套可替换的通用中心 OCV 表：

- `BMS_SOC_CHEMISTRY_LFP`：磷酸铁锂；
- `BMS_SOC_CHEMISTRY_NMC`：三元；
- `BMS_SOC_CHEMISTRY_AUTO`：默认模式。依据已加载的三级单体过压参数自动选择：`<= 3900 mV` 视为 LFP，`> 3900 mV` 视为 NMC。

也可以通过 `bms_soc_configure()` 显式固定 chemistry。当前 D011 默认保护参数为 3750 mV，因此 AUTO 会选择 LFP；如果产品改为三元并把三级 OVP 配置到 4.2 V 区域，SOC 会自动切换 NMC profile。

OCV 表是“通用中心表”，不是某一型号电芯的实验标定曲线。量产项目如果有电芯厂家/实测静置曲线，应只替换 profile 表，不改算法。

## 3. OCV 置信区间

OCV 使用保守加权单体电压：

```text
V_ocv = (3 * Vcell_min + Vcell_max) / 4
```

静置校准条件：

- 充/放电有效电流均低于 200 mA；
- 单体压差 <= 100 mV；
- 相邻 200 ms 样本变化 <= 8 mV；
- 连续稳定 **>= 10 min**。

达到条件后，由化学体系中心表得到 `center SOC`，再形成默认 `center ± 5 percentage points` 的置信区间 `[low, high]`。

关键规则：

- estimate 在区间内：不修正；
- estimate 低于 `low`：**绝不通过普通 OCV 向上校准**；
- estimate 高于 `high`：每 30 min 最多下降 1%，长期缓慢回归到 `high` 边界；
- 唯一允许主动向上拉 SOC 的路径是确认满充锚点。

因此开机后会自动进入 OCV 准备判定，但不会因为一次开机电压读数直接跳 SOC。

## 4. 满 / 空锚点与低端体验

满电：

- 三级单体 OVP 已触发时，estimate 强锚定 100%；或
- 根据 chemistry profile 的满电电压条件稳定 60 s，之后约 2 s/1% 向 100% 收敛。

空电：

- 三级单体 UVP 已触发时，SOC 强锚定 0%；
- 放电低端提前分段收敛，避免到 UVP 时从较高 SOC 突然掉 0；
- LFP 与 NMC 使用不同低端 knee：LFP 不会再把 3.30 V 当成 12% 低端区。

高放电电流导致的压降会触发 sag hold；普通低端电压修正被抑制，1%/0% 安全端点仍保留。

## 5. SOC Low 告警

`g_tParam.protect.u16SocUp_First/Second/Third` 沿用历史字段名，但实际按低 SOC 阈值处理，并写入 First/Second/Third 的 `b1SocLow`。恢复值若低于对应 trip，会自动提升为 `trip + 1%`，避免原参数组合造成抖动。

SOC Low 目前只形成告警/故障位，不直接关闭 DSG MOS；是否把三级 SOC Low 变成保护动作应由产品策略单独决定。

## 6. 容量学习

框架已经实现，但 **默认关闭**：

- 0% 锚点 -> 完整充到 100%；
- 100% 锚点 -> 完整放到 0%；
- 中途出现反向电流或 MCU 重启，本次学习作废；
- 成功值必须在标称容量 50%~130% 的合理范围内；
- 学习成功后容量与 learned flag 写入 hot KV。

当 `capacity_learning_enable=1` 且 `hide_capacity_until_learned=1` 时，首次学习成功之前容量字段报告 0；SOC 百分比仍正常显示。默认学习关闭时继续报告标称/估算容量，不改变当前产品行为。

## 7. 诊断接口

`bms_soc_get_diag()` 可读取：chemistry、estimate/display SOC、OCV 状态、center/low/high、OCV 电压、静置秒数、confidence、容量学习状态和 learned capacity，便于以后直接映射到 Modbus/BLE 诊断寄存器。

## 8. 必测场景

1. LFP/NMC 两种 profile 的 0/100% 锚点和 OCV 表切换。
2. 0.1 A 不积分、0.2 A 开始积分的边界。
3. 静置 9 min 59 s 不校准，10 min 后只允许向下；长期只回归到 high 边界。
4. 充电到满、放电到 UVP，显示 SOC 不产生普通大跳变。
5. LFP 3.30 V 中平台不得误判为低端 12%。
6. 5 A / 10 A / 20 A 放电压降与松油门回弹。
7. SOC Low 20/10/5% 三级告警及恢复滞回。
8. Flash 掉电恢复；容量学习成功/中断/复位作废。
'''

SOC_TEST = r'''#!/usr/bin/env python3
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
C = (MOD / "SocEnhance.c").read_text(encoding="utf-8", errors="ignore")
H = (MOD / "SocEnhance.h").read_text(encoding="utf-8", errors="ignore")
KV_C = (MOD / "soc_kv_store.c").read_text(encoding="utf-8", errors="ignore")
KV_H = (MOD / "soc_kv_store.h").read_text(encoding="utf-8", errors="ignore")

class SocContract(unittest.TestCase):
    def test_dual_chemistry_profiles_exist(self):
        self.assertIn("BMS_SOC_CHEMISTRY_LFP", H)
        self.assertIn("BMS_SOC_CHEMISTRY_NMC", H)
        self.assertIn("g_soc_ocv_lfp", C)
        self.assertIn("g_soc_ocv_nmc", C)
        self.assertIn("SOC_AUTO_LFP_OVP_MAX_MV              3900u", C)

    def test_coulomb_integration_and_deadband(self):
        self.assertIn("SOC_INTEGRAL_PERIOD_MS              200u", C)
        self.assertIn("SOC_CURRENT_DEADBAND_MA_DEFAULT     200u", C)
        self.assertIn("soc_current_direction", C)
        self.assertIn("g_soc_integral_ms_remainder", C)

    def test_ocv_requires_ten_minutes_and_uses_band(self):
        self.assertIn("SOC_OCV_REST_PREPARE_SECONDS        600u", C)
        self.assertIn("SOC_OCV_ERROR_BAND_PERCENT          5u", C)
        self.assertIn("g_soc_runtime.ocv_low", C)
        self.assertIn("g_soc_runtime.ocv_high", C)
        self.assertIn("return soc_step_down_to(g_soc_runtime.ocv_high);", C)
        ocv_fn = C[C.index("static uint8_t soc_idle_ocv_tracking"):C.index("static uint16_t soc_discharge_natural_1pct_ticks")]
        self.assertNotIn("soc_step_up_to", ocv_fn)

    def test_display_soc_is_separate(self):
        self.assertIn("static uint8_t g_soc_display_soc", C)
        self.assertIn("SOC_DISPLAY_STEP_TICKS              SOC_TICKS_PER_SECOND", C)
        self.assertIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_display();", C)

    def test_endpoints_and_lfp_terminal_knee_are_chemistry_specific(self):
        self.assertIn("g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp", C)
        self.assertIn("g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp", C)
        self.assertIn("150u, 100u, 50u, 20u", C)
        self.assertIn("300u, 200u, 150u, 50u", C)

    def test_soc_low_faults_are_implemented_without_mos_policy(self):
        self.assertIn("soc_update_low_faults", C)
        self.assertIn("fault->bits.b1SocLow", C)
        self.assertIn("u16SocUp_First", C)
        self.assertNotIn("b1SocLow ||", C)

    def test_learning_is_present_but_default_disabled(self):
        self.assertIn("BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u", C)
        self.assertIn("BMS_SOC_LEARNING_EMPTY_TO_FULL", H)
        self.assertIn("BMS_SOC_LEARNING_FULL_TO_EMPTY", H)
        self.assertIn("soc_learning_on_full_anchor", C)
        self.assertIn("soc_learning_on_empty_anchor", C)
        self.assertIn("SOC_KV_FLAG_CAPACITY_LEARNED", KV_H)
        self.assertIn("SOC_KV_KEY_LEARNED_CAPACITY", KV_C)
        self.assertIn("soc_kv_store_write_learning", KV_C)

    def test_diag_api_exists(self):
        self.assertIn("bms_soc_diag_t", H)
        self.assertIn("void bms_soc_get_diag", C)

if __name__ == "__main__":
    unittest.main(verbosity=2)
'''

(MOD / "SocEnhance.h").write_text(SOC_H, encoding="utf-8")
(MOD / "SocEnhance.c").write_text(SOC_C, encoding="utf-8")
(MOD / "soc_kv_store.h").write_text(SOC_KV_H, encoding="utf-8")
(MOD / "soc_kv_store.c").write_text(SOC_KV_C, encoding="utf-8")
(ROOT / "docs" / "SOC.md").write_text(SOC_DOC, encoding="utf-8")
(ROOT / "tests" / "soc_contract_check.py").write_text(SOC_TEST, encoding="utf-8")

# Replace stale source-contract expectations for the previous SOC algorithm.
quick = ROOT / "tests" / "flash_quick_check.py"
text = quick.read_text(encoding="utf-8")
start = text.index("    def test_soc_cycle_recalcs_capacity_after_soh_change(self):")
end = text.index("    def test_afe_read_failure_freezes_soc_current_and_preserves_soc_report(self):", start)
replacement = r'''    def test_soc_core_contracts_match_current_design(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("#define SOC_EQUIV_CYCLE_PERCENT             100u", text)
        self.assertIn("#define SOC_INTEGRAL_PERIOD_MS              200u", text)
        self.assertIn("#define SOC_CURRENT_DEADBAND_MA_DEFAULT     200u", text)
        self.assertIn("#define SOC_OCV_REST_PREPARE_SECONDS        600u", text)
        self.assertIn("#define SOC_OCV_ERROR_BAND_PERCENT          5u", text)
        self.assertIn("static const soc_ocv_point_t g_soc_ocv_lfp[]", text)
        self.assertIn("static const soc_ocv_point_t g_soc_ocv_nmc[]", text)
        self.assertIn("return soc_step_down_to(g_soc_runtime.ocv_high);", text)
        self.assertIn("static uint8_t g_soc_display_soc", text)
        self.assertIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_display();", text)
        self.assertIn("soc_update_low_faults();", text)

    def test_soc_capacity_learning_persistence_contract(self):
        soc_text = read_text(SOC_ENHANCE_C)
        kv_h = read_text(SOC_KV_H)
        kv_c = read_text(SOC_KV_C)
        self.assertIn("BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u", soc_text)
        self.assertIn("SOC_KV_FLAG_CAPACITY_LEARNED", kv_h)
        self.assertIn("SOC_KV_KEY_LEARNED_CAPACITY", kv_c)
        self.assertIn("soc_kv_store_write_learning", kv_c)

'''
text = text[:start] + replacement + text[end:]
quick.write_text(text, encoding="utf-8")

# Wire the dedicated SOC contract into both host and TC32 CI, plus py_compile.
ci = ROOT / ".github" / "workflows" / "bms-ci.yml"
ci_text = ci.read_text(encoding="utf-8")
if "Run SOC module contract check" not in ci_text:
    ci_text = ci_text.replace(
        "      - name: Run Flash storage contract check\n        run: python tests/flash_quick_check.py\n",
        "      - name: Run SOC module contract check\n        run: python tests/soc_contract_check.py\n\n      - name: Run Flash storage contract check\n        run: python tests/flash_quick_check.py\n",
        1,
    )
    ci_text = ci_text.replace(
        "tests/sh3673510_d011_integration_check.py tests/flash_quick_check.py",
        "tests/sh3673510_d011_integration_check.py tests/soc_contract_check.py tests/flash_quick_check.py",
        1,
    )
    tc32_marker = "      - name: Run Flash storage contract check\n        shell: powershell\n        run: python tests/flash_quick_check.py\n"
    ci_text = ci_text.replace(
        tc32_marker,
        "      - name: Run SOC module contract check\n        shell: powershell\n        run: python tests/soc_contract_check.py\n\n" + tc32_marker,
        1,
    )
ci.write_text(ci_text, encoding="utf-8")

# Temporary migration files must not persist in the product branch.
for rel in ["tools/refactor_soc_module.py", ".github/workflows/soc-module-refactor.yml"]:
    p = ROOT / rel
    if p.exists():
        p.unlink()
