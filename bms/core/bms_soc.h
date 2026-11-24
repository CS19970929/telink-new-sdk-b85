#ifndef BMS_SOC_H_
#define BMS_SOC_H_

#include <stdint.h>
#include "bms_soc_defs.h"

typedef enum {
    BMS_SOC_NO_ENDPOINT = 0,
    BMS_SOC_FULL,
    BMS_SOC_EMPTY
} bms_soc_endpoint_t;

typedef struct {
    uint16_t full_mv;
    uint16_t empty_mv;
} bms_soc_limits_t;

typedef struct {
    bms_soc_limits_t limits;
    uint16_t capacity_0p1ah;
    uint8_t chemistry;
    uint8_t cell_count;
} bms_soc_config_t;

/* Flash 按字段编码。检查点保存高精度电量、显示和可信端点；容量为零表示尚无检查点。 */
typedef struct {
    uint32_t charge_as10;
    uint32_t discharge_as10;
    uint32_t endpoint_basis;
    uint16_t capacity_0p1ah;
    uint16_t cycles;
    uint8_t shown;
    uint8_t endpoint;
    uint8_t estimated;
} bms_soc_checkpoint_t;

typedef struct {
    uint32_t now_ms;
    uint32_t current_time_ms;
    int32_t current_ma;
    uint16_t min_mv;
    uint16_t max_mv;
    uint16_t mean_mv;
    uint16_t temperature_x10;
    uint8_t current_valid;
    uint8_t voltage_valid;
    uint8_t voltage_fresh;
    uint8_t charge_cutoff;
    uint8_t discharge_cutoff;
    uint8_t resting_allowed;
    uint8_t line_check_active;
    uint8_t line_check_passed;
} bms_soc_sample_t;

typedef struct {
    bms_soc_config_t config;
    bms_soc_checkpoint_t state;
    int32_t carry_ma_ms;
    int32_t previous_ma;
    uint32_t previous_ms;
    uint32_t previous_current_ms;
    uint32_t voltage_ms;
    uint32_t drive_ms;
    uint32_t near_start_ms;
    uint32_t rest_start_ms;
    uint32_t init_start_ms;
    uint16_t display_ms;
    uint16_t rest_min_mv, rest_max_mv, rest_mean_mv, rest_temp_x10;
    uint16_t near_min_mv;
    uint16_t near_elapsed_ms;
    int8_t drive;
    uint8_t previous_valid;
    uint8_t near;
    uint8_t resting;
    uint8_t has_voltage;
    uint8_t initial_estimate_pending;
    uint8_t cutoff;
    uint8_t pending_cutoff;
    uint8_t weak_check;
} bms_soc_t;

void bms_soc_defaults(uint8_t chemistry, bms_soc_limits_t *limits);
uint8_t bms_soc_limits_valid(uint8_t chemistry, const bms_soc_limits_t *limits);
uint8_t bms_soc_checkpoint_valid(const bms_soc_checkpoint_t *checkpoint);
void bms_soc_init(bms_soc_t *soc, const bms_soc_config_t *config,
                  const bms_soc_checkpoint_t *checkpoint, uint32_t now_ms);
void bms_soc_reconfigure(bms_soc_t *soc, const bms_soc_config_t *config);
/* 返回 1 时应用可排队一次弱串验证。线路检查完成后由样本交回结果。 */
uint8_t bms_soc_step(bms_soc_t *soc, const bms_soc_sample_t *sample);
uint8_t bms_soc_percent(const bms_soc_t *soc);

#endif
