/* SOC 算法参数与 OCV 曲线的独立持久化；不改变 CFG2 payload。 */
#pragma once

#include <stdint.h>

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

/* 启动时加载一次；空白、损坏或类型不匹配时只使用编译默认，不自动写 Flash。 */
void bms_soc_profile_store_init(void);
void bms_soc_profile_store_defaults(uint8_t chemistry, bms_soc_profile_config_t *config);
int bms_soc_profile_store_valid(const bms_soc_profile_config_t *config);
/* main-loop 同步只读视图；对象地址固定，init/set/reset 成功后内容可变。
 * 消费者用 generation 识别发布，不持有 stage；不得跨并发写入使用。 */
const bms_soc_profile_config_t *bms_soc_profile_store_current(void);
void bms_soc_profile_store_get(bms_soc_profile_config_t *config);
int bms_soc_profile_store_set(const bms_soc_profile_config_t *config);
int bms_soc_profile_store_reset(uint8_t chemistry);
uint16_t bms_soc_profile_store_word(const bms_soc_profile_config_t *config, uint16_t offset);
int bms_soc_profile_store_set_word(bms_soc_profile_config_t *config, uint16_t offset, uint16_t value);
uint16_t bms_soc_profile_store_generation(void);
uint8_t bms_soc_profile_store_is_persisted(void);
