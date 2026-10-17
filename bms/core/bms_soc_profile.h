/* SOC 默认曲线与已提交曲线使用同一定长表示；不改变 50-word 持久/协议布局。 */
#pragma once
#include "bms_soc_defs.h"
#include "bms_product.h"
#include "bms_soc_profile_store.h"

/* 通用中心曲线仅为默认，不声明电芯模型精度。点数由实际初始化表推导。 */
static const bms_soc_profile_config_t g_soc_profile_lfp = {
    .chemistry = BMS_SOC_CHEMISTRY_LFP,
    .profile_id = BMS_SOC_PROFILE_GENERIC_LFP,
    .profile_version = BMS_SOC_PROFILE_GENERIC_LFP_VERSION,
    .point_count = sizeof((bms_soc_ocv_config_point_t[]){BMS_SOC_OCV_LFP_POINTS}) /
                   sizeof(bms_soc_ocv_config_point_t),
    .current_deadband_ma = BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT,
    .ocv_rest_prepare_s = BMS_SOC_OCV_REST_PREPARE_S_DEFAULT,
    .ocv_error_band_percent = BMS_SOC_OCV_ERROR_BAND_PERCENT_DEFAULT,
    .valid_min_mv = 2500u, .valid_max_mv = 3800u,
    .full_sync_mv = 3500u, .full_min_margin_mv = 100u,
    .empty_sync_mv = 3000u, .empty_max_margin_mv = 150u,
    .ocv_min_error_band_percent = 7u, .full_cell_delta_max_mv = 80u,
    .terminal_cell_delta_max_mv = 50u,
    .terminal_start_offset_mv = 150u, .terminal_l1_offset_mv = 100u,
    .terminal_l2_offset_mv = 50u, .terminal_l3_offset_mv = 20u,
    .points = {BMS_SOC_OCV_LFP_POINTS},
};

static const bms_soc_profile_config_t g_soc_profile_nmc = {
    .chemistry = BMS_SOC_CHEMISTRY_NMC,
    .profile_id = BMS_SOC_PROFILE_GENERIC_NMC,
    .profile_version = BMS_SOC_PROFILE_GENERIC_NMC_VERSION,
    .point_count = sizeof((bms_soc_ocv_config_point_t[]){BMS_SOC_OCV_NMC_POINTS}) /
                   sizeof(bms_soc_ocv_config_point_t),
    .current_deadband_ma = BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT,
    .ocv_rest_prepare_s = BMS_SOC_OCV_REST_PREPARE_S_DEFAULT,
    .ocv_error_band_percent = BMS_SOC_OCV_ERROR_BAND_PERCENT_DEFAULT,
    .valid_min_mv = 2600u, .valid_max_mv = 4300u,
    .full_sync_mv = 4180u, .full_min_margin_mv = 200u,
    .empty_sync_mv = 3000u, .empty_max_margin_mv = 200u,
    .ocv_min_error_band_percent = 5u, .full_cell_delta_max_mv = 80u,
    .terminal_cell_delta_max_mv = 50u,
    .terminal_start_offset_mv = 300u, .terminal_l1_offset_mv = 200u,
    .terminal_l2_offset_mv = 150u, .terminal_l3_offset_mv = 50u,
    .points = {BMS_SOC_OCV_NMC_POINTS},
};
