#include "bms_soc_profile.h"
#include "bms_product.h"
#include <string.h>

/* 通用中心曲线仅为默认，不声明电芯模型精度。点数由实际初始化表推导。 */
const bms_soc_profile_config_t g_soc_profile_lfp = {
    .chemistry = BMS_SOC_CHEMISTRY_LFP,
    .profile_id = BMS_SOC_PROFILE_GENERIC_LFP,
    .profile_version = BMS_SOC_PROFILE_GENERIC_LFP_VERSION,
    .point_count = 14u,
    .current_deadband_ma = BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT,
    .ocv_rest_prepare_s = BMS_SOC_OCV_REST_MS / 1000u,
    .ocv_error_band_percent = 7u,
    .valid_min_mv = 2000u, .valid_max_mv = 3800u,
    .full_sync_mv = 3500u, .full_min_margin_mv = 200u,
    .empty_sync_mv = 2850u, .empty_max_margin_mv = 300u,
    .ocv_min_error_band_percent = 7u, .full_cell_delta_max_mv = 200u,
    .terminal_cell_delta_max_mv = 50u,
    .terminal_start_offset_mv = 150u, .terminal_l1_offset_mv = 100u,
    .terminal_l2_offset_mv = 50u, .terminal_l3_offset_mv = 20u,
    .points = {{2844u,0u},{3163u,5u},{3204u,10u},{3223u,15u},{3245u,20u},
               {3259u,25u},{3277u,30u},{3288u,35u},{3291u,55u},{3294u,60u},
               {3315u,65u},{3328u,75u},{3330u,95u},{3445u,100u}},
};

const bms_soc_profile_config_t g_soc_profile_nmc = {
    .chemistry = BMS_SOC_CHEMISTRY_NMC,
    .profile_id = BMS_SOC_PROFILE_GENERIC_NMC,
    .profile_version = BMS_SOC_PROFILE_GENERIC_NMC_VERSION,
    .point_count = 13u,
    .current_deadband_ma = BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT,
    .ocv_rest_prepare_s = BMS_SOC_OCV_REST_MS / 1000u,
    .ocv_error_band_percent = 5u,
    .valid_min_mv = 2000u, .valid_max_mv = 4300u,
    .full_sync_mv = 4180u, .full_min_margin_mv = 200u,
    .empty_sync_mv = 3100u, .empty_max_margin_mv = 300u,
    .ocv_min_error_band_percent = 5u, .full_cell_delta_max_mv = 200u,
    .terminal_cell_delta_max_mv = 50u,
    .terminal_start_offset_mv = 300u, .terminal_l1_offset_mv = 200u,
    .terminal_l2_offset_mv = 150u, .terminal_l3_offset_mv = 50u,
    .points = {{3000u,0u},{3300u,5u},{3450u,10u},{3550u,20u},{3650u,30u},
               {3700u,40u},{3750u,50u},{3800u,60u},{3850u,70u},{3900u,80u},
               {4000u,90u},{4100u,96u},{4180u,100u}},
};

static const bms_soc_profile_config_t *profile_for_chemistry(uint8_t chemistry)
{
    return chemistry == BMS_SOC_CHEMISTRY_LFP ? &g_soc_profile_lfp :
        chemistry == BMS_SOC_CHEMISTRY_NMC ? &g_soc_profile_nmc : 0;
}

void bms_soc_profile_defaults(uint8_t chemistry, bms_soc_profile_config_t *config)
{
    const bms_soc_profile_config_t *profile = profile_for_chemistry(chemistry);
    if (!config) return;
    memset(config, 0, sizeof(*config));
    if (profile) *config = *profile;
}

int bms_soc_profile_valid(const bms_soc_profile_config_t *c)
{
    uint16_t i;
    if (!c || (c->chemistry != BMS_SOC_CHEMISTRY_LFP &&
               c->chemistry != BMS_SOC_CHEMISTRY_NMC) ||
        c->profile_id != c->chemistry || c->profile_version == 0u ||
        c->point_count < 2u || c->point_count > BMS_SOC_PROFILE_MAX_POINTS ||
        c->current_deadband_ma > 2000u ||
        c->ocv_rest_prepare_s < 60u || c->ocv_rest_prepare_s > 3600u ||
        c->ocv_error_band_percent == 0u || c->ocv_error_band_percent > 20u ||
        c->valid_min_mv < 1000u || c->valid_min_mv >= c->valid_max_mv ||
        c->valid_max_mv > 5000u || c->empty_sync_mv < c->valid_min_mv ||
        c->empty_sync_mv >= c->full_sync_mv || c->full_sync_mv > c->valid_max_mv ||
        c->full_min_margin_mv > 1000u || c->empty_max_margin_mv > 1000u ||
        c->ocv_min_error_band_percent == 0u || c->ocv_min_error_band_percent > 20u ||
        c->full_cell_delta_max_mv > 2000u || c->terminal_cell_delta_max_mv > 2000u ||
        c->terminal_start_offset_mv < c->terminal_l1_offset_mv ||
        c->terminal_l1_offset_mv < c->terminal_l2_offset_mv ||
        c->terminal_l2_offset_mv < c->terminal_l3_offset_mv) return 0;
    /* v3 静置时长属于固定策略，拒绝接受后被悄悄忽略的旧时间值。 */
    if (c->profile_version == BMS_SOC_POLICY_VERSION &&
        c->ocv_rest_prepare_s != BMS_SOC_OCV_REST_MS / 1000u) return 0;
    if (c->points[0].soc_percent != 0u ||
        c->points[c->point_count - 1u].soc_percent != 100u) return 0;
    for (i = 0u; i < c->point_count; ++i) {
        if (c->points[i].mv < c->valid_min_mv || c->points[i].mv > c->valid_max_mv ||
            c->points[i].soc_percent > 100u) return 0;
        if (i != 0u && (c->points[i].mv <= c->points[i - 1u].mv ||
                        c->points[i].soc_percent < c->points[i - 1u].soc_percent)) return 0;
    }
    return 1;
}

uint16_t bms_soc_profile_word(const bms_soc_profile_config_t *c, uint16_t offset)
{
    uint16_t value = 0u;
    if (!c || offset >= BMS_SOC_PROFILE_WORDS) return 0u;
    if (offset < BMS_SOC_PROFILE_FIXED_WORDS) {
        memcpy(&value, (const uint8_t *)c + offset * 2u, sizeof(value));
        return value;
    }
    offset -= BMS_SOC_PROFILE_FIXED_WORDS;
    return (offset & 1u) ? c->points[offset / 2u].soc_percent : c->points[offset / 2u].mv;
}

int bms_soc_profile_set_word(bms_soc_profile_config_t *c, uint16_t offset, uint16_t value)
{
    if (!c || offset >= BMS_SOC_PROFILE_WORDS) return 0;
    if (offset < BMS_SOC_PROFILE_FIXED_WORDS) {
        memcpy((uint8_t *)c + offset * 2u, &value, sizeof(value));
        return 1;
    }
    offset -= BMS_SOC_PROFILE_FIXED_WORDS;
    if (offset & 1u) c->points[offset / 2u].soc_percent = value;
    else c->points[offset / 2u].mv = value;
    return 1;
}

