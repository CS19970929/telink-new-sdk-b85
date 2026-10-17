/* SOC 曲线类型、编译默认、业务校验和协议 word 映射；持久配置由 Config 拥有。 */
#pragma once
#include <stdint.h>
#include "bms_soc_defs.h"
#define BMS_SOC_PROFILE_MAX_POINTS 15u
#define BMS_SOC_PROFILE_FIXED_WORDS 20u
#define BMS_SOC_PROFILE_WORDS \
    (BMS_SOC_PROFILE_FIXED_WORDS + BMS_SOC_PROFILE_MAX_POINTS * 2u)

typedef struct {
    uint16_t mv;
    uint16_t soc_percent;
} bms_soc_ocv_config_point_t;

typedef struct {
    uint16_t chemistry;
    uint16_t profile_id;
    uint16_t profile_version;
    uint16_t point_count;
    uint16_t current_deadband_ma;
    uint16_t ocv_rest_prepare_s;
    uint16_t ocv_error_band_percent;
    uint16_t valid_min_mv;
    uint16_t valid_max_mv;
    uint16_t full_sync_mv;
    uint16_t full_min_margin_mv;
    uint16_t empty_sync_mv;
    uint16_t empty_max_margin_mv;
    uint16_t ocv_min_error_band_percent;
    uint16_t full_cell_delta_max_mv;
    uint16_t terminal_cell_delta_max_mv;
    uint16_t terminal_start_offset_mv;
    uint16_t terminal_l1_offset_mv;
    uint16_t terminal_l2_offset_mv;
    uint16_t terminal_l3_offset_mv;
    bms_soc_ocv_config_point_t points[BMS_SOC_PROFILE_MAX_POINTS];
} bms_soc_profile_config_t;


extern const bms_soc_profile_config_t g_soc_profile_lfp;
extern const bms_soc_profile_config_t g_soc_profile_nmc;
void bms_soc_profile_defaults(uint8_t chemistry, bms_soc_profile_config_t *config);
int bms_soc_profile_valid(const bms_soc_profile_config_t *config);
uint16_t bms_soc_profile_word(const bms_soc_profile_config_t *config, uint16_t offset);
int bms_soc_profile_set_word(bms_soc_profile_config_t *config, uint16_t offset, uint16_t value);
