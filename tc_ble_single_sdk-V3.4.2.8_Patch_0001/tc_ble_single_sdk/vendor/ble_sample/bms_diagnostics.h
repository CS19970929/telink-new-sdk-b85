#pragma once

#include <stdint.h>
#include "bms_measurement.h"

enum {
    BMS_DIAG_CELL_RANGE          = 1UL << 0,
    BMS_DIAG_CELL_JUMP           = 1UL << 1,
    BMS_DIAG_CELL_FROZEN         = 1UL << 2,
    BMS_DIAG_CELL_COUNT          = 1UL << 3,
    BMS_DIAG_CELL_MAP            = 1UL << 4,
    BMS_DIAG_CELL_SUM_AFE        = 1UL << 5,
    BMS_DIAG_CELL_SUM_ADC        = 1UL << 6,
    BMS_DIAG_PACK_RANGE          = 1UL << 7,
    BMS_DIAG_PACK_CROSS          = 1UL << 8,
    BMS_DIAG_CURRENT_SAT         = 1UL << 9,
    BMS_DIAG_CURRENT_JUMP        = 1UL << 10,
    BMS_DIAG_CURRENT_FROZEN      = 1UL << 11,
    BMS_DIAG_CURRENT_ZERO        = 1UL << 12,
    BMS_DIAG_CURRENT_DIRECTION   = 1UL << 13,
    BMS_DIAG_TEMP_OPEN           = 1UL << 14,
    BMS_DIAG_TEMP_SHORT          = 1UL << 15,
    BMS_DIAG_TEMP_JUMP           = 1UL << 16,
    BMS_DIAG_NTC_MISMATCH        = 1UL << 17,
    BMS_DIAG_SAMPLE_STALE        = 1UL << 18,
    BMS_DIAG_AFE_COMM            = 1UL << 19,
    BMS_DIAG_AFE_CONFIG          = 1UL << 20,
    BMS_DIAG_BOARD_SERIES        = 1UL << 21,
    BMS_DIAG_PARAM_CRC           = 1UL << 22,
    BMS_DIAG_RSENSE_CONFIG       = 1UL << 23
};

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_jump_mv;
    uint16_t pack_cross_max_mv;
    uint16_t cell_sum_max_error_mv;
    uint32_t current_jump_ma;
    uint16_t current_zero_drift_ma;
    uint16_t temp_jump_dC;
    uint16_t ntc_open_mv;
    uint16_t ntc_short_mv;
    uint16_t frozen_frames;
    uint8_t expected_series;
    uint8_t board_valid;
    uint8_t cell_map_valid;
    uint8_t params_valid;
    uint8_t rsense_valid;
} bms_diagnostic_config_t;

void bms_diagnostics_config_finalize(bms_diagnostic_config_t *cfg);
uint8_t bms_diagnostics_config_validate(const bms_diagnostic_config_t *cfg);
void bms_diagnostics_init(const bms_diagnostic_config_t *cfg);
uint32_t bms_diagnostics_update(const bms_measurement_t *m, uint32_t now_ms);
void bms_diagnostics_set_afe_config_valid(uint8_t valid);
uint32_t bms_diagnostics_faults(void);
uint32_t bms_diagnostics_blocking_faults(void);
uint32_t bms_diagnostics_invalid_cell_mask(void);
