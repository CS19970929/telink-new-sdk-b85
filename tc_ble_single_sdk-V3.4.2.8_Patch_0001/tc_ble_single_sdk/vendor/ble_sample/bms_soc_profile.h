#pragma once

#include "conf.h"
#include "bms_soc_defs.h"

/*
 * OCV/profile data lives outside the SOC algorithm on purpose. Product-specific
 * cell characterization should replace/add profile data here without rewriting
 * coulomb integration, confidence-window, persistence, or display behavior.
 */
typedef struct
{
    uint16_t mv;
    uint8_t soc;
} soc_ocv_point_t;

typedef struct
{
    uint8_t profile_id;
    uint16_t profile_version;
    uint8_t chemistry;
    const soc_ocv_point_t *ocv;
    uint8_t ocv_count;
    uint16_t valid_min_mv;
    uint16_t valid_max_mv;
    uint16_t full_sync_mv;
    uint16_t full_min_margin_mv;
    uint16_t empty_sync_mv;
    uint16_t empty_max_margin_mv;
    uint8_t ocv_min_error_band_percent;
    uint16_t full_cell_delta_max_mv;
    uint16_t learning_cell_delta_max_mv;
    uint16_t learning_endpoint_max_c_rate_x1000;
    uint16_t terminal_start_offset_mv;
    uint16_t terminal_l1_offset_mv;
    uint16_t terminal_l2_offset_mv;
    uint16_t terminal_l3_offset_mv;
} soc_profile_t;

/* Generic center curves. These are defaults, not a claim of cell-model accuracy. */
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
    BMS_SOC_PROFILE_GENERIC_LFP,
    BMS_SOC_PROFILE_GENERIC_LFP_VERSION,
    BMS_SOC_CHEMISTRY_LFP,
    g_soc_ocv_lfp,
    (uint8_t)(sizeof(g_soc_ocv_lfp) / sizeof(g_soc_ocv_lfp[0])),
    2500u, 3800u,
    3500u, 100u,
    3000u, 150u,
    7u, 80u, 50u, 50u,
    150u, 100u, 50u, 20u,
};

static const soc_profile_t g_soc_profile_nmc = {
    BMS_SOC_PROFILE_GENERIC_NMC,
    BMS_SOC_PROFILE_GENERIC_NMC_VERSION,
    BMS_SOC_CHEMISTRY_NMC,
    g_soc_ocv_nmc,
    (uint8_t)(sizeof(g_soc_ocv_nmc) / sizeof(g_soc_ocv_nmc[0])),
    2600u, 4300u,
    4180u, 200u,
    3000u, 200u,
    5u, 80u, 50u, 50u,
    300u, 200u, 150u, 50u,
};
