/*
 * 文件功能：SOC 电芯化学体系/OCV 曲线和算法配置档案；用于电压估算与容量参数边界。
 * bms/core/bms_soc_profile.h；实际编译归属见各产品 sources.txt。
 */
#pragma once


#include "bms_soc_defs.h"
#include "bms_product.h"

/*
 * OCV/配置数据有意独立于 SOC 算法；电芯特性应在此替换/增加数据，
 * 不重写库仑积分、置信窗口、持久化或显示行为。
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
    uint16_t terminal_cell_delta_max_mv;
    uint16_t terminal_start_offset_mv;
    uint16_t terminal_l1_offset_mv;
    uint16_t terminal_l2_offset_mv;
    uint16_t terminal_l3_offset_mv;
} soc_profile_t;

/* 通用中心曲线仅为默认，不声明电芯模型精度。 */
static const soc_ocv_point_t g_soc_ocv_lfp[] = {
    BMS_SOC_OCV_LFP_POINTS,
};

static const soc_ocv_point_t g_soc_ocv_nmc[] = {
    BMS_SOC_OCV_NMC_POINTS,
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
    7u, 80u, 50u,
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
    5u, 80u, 50u,
    300u, 200u, 150u, 50u,
};
