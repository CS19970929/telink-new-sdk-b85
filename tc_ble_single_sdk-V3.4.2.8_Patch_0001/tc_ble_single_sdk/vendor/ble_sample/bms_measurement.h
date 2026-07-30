#pragma once

#include <stdint.h>
#include "bms_safety_config.h"

typedef struct {
    uint16_t cell_mv[BMS_CELL_MAX];
    uint32_t cell_valid_mask;
    uint32_t pack_mv_afe;
    uint32_t pack_mv_adc;
    int32_t current_ma;              /* positive: charge, negative: discharge */
    int16_t battery_temp_dC;
    int16_t mos_temp_dC;
    uint16_t current_raw;
    uint16_t battery_ntc_mv;
    uint16_t mos_ntc_mv;
    uint8_t series_count;
    uint8_t afe_frame_valid;
    uint8_t pack_adc_valid;
    uint8_t current_valid;
    uint8_t battery_temp_valid;
    uint8_t mos_temp_valid;
    uint8_t charger_present;
    uint8_t load_requested;
    uint32_t afe_timestamp_ms;
    uint32_t adc_timestamp_ms;
    uint32_t timestamp_ms;
} bms_measurement_t;

void bms_measurement_init(void);
void bms_measurement_publish(const bms_measurement_t *measurement);
void bms_measurement_invalidate_all(uint32_t now_ms);
void bms_measurement_note_wakeup(uint32_t now_ms);
const bms_measurement_t *bms_measurement_get(void);
uint8_t bms_measurement_is_fresh_and_complete(uint32_t now_ms);
