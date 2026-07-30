#pragma once

#include <stdint.h>
#include "bms_measurement.h"

typedef enum {
    FET_MONITOR_IDLE = 0,
    FET_OFF_REQUESTED,
    FET_OFF_GRACE,
    FET_OFF_VERIFY,
    FET_OFF_CONFIRMED,
    FET_OFF_FAILED
} bms_fet_monitor_state_t;

typedef enum {
    BMS_FET_CHARGE = 0,
    BMS_FET_DISCHARGE,
    BMS_FET_CTLC,
    BMS_FET_MONITOR_COUNT
} bms_fet_path_t;

typedef struct {
    bms_fet_monitor_state_t state;
    uint32_t requested_ms;
    uint8_t danger_frames;
    uint8_t was_commanded_on;
} bms_fet_monitor_t;

void bms_fet_monitor_init(void);
void bms_fet_monitor_update(const bms_measurement_t *m, uint32_t now_ms,
                            uint8_t afe_charge_on, uint8_t afe_discharge_on);
uint8_t bms_fet_monitor_any_failed(void);
const bms_fet_monitor_t *bms_fet_monitor_get(bms_fet_path_t path);

/* Optional feedback adapter. Unsupported hardware must return zero. */
uint8_t bms_fet_hw_feedback_supported(bms_fet_path_t path);
uint8_t bms_fet_hw_is_off(bms_fet_path_t path);
