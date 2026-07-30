#pragma once

#include <stdint.h>
#include "bms_measurement.h"

typedef enum {
    FUSE_DISABLED = 0,
    FUSE_MONITORING,
    FUSE_ARMED,
    FUSE_TRIGGERING,
    FUSE_FIRED,
    FUSE_FAILED
} bms_fuse_state_t;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t severe_hold_ms;
    uint32_t max_pulse_ms;
    uint32_t crc32;
    uint8_t required_evidence_count;
    uint8_t auto_trigger_enable;
    uint8_t hardware_feedback_enable;
    uint8_t factory_test_enable;
} bms_fuse_config_t;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t state;
    uint32_t reason;
    uint32_t fault_time_ms;
    uint16_t cell_max_mv;
    uint16_t cell_min_mv;
    uint32_t pack_mv;
    int32_t current_ma;
    int16_t battery_temp_dC;
    int16_t mos_temp_dC;
    uint16_t afe_status;
    uint16_t reset_reason;
    uint16_t param_version;
    uint16_t reserved;
    uint32_t crc32;
} bms_fuse_record_t;

typedef struct {
    uint32_t evidence_mask;
    uint32_t severe_since_ms;
    uint8_t fet_off_failed;
    uint8_t environment_valid;
    uint8_t factory_or_debug;
    uint8_t hardware_authorized;
} bms_fuse_input_t;

void bms_fuse_init(uint32_t now_ms);
void bms_fuse_set_context(uint16_t afe_status, uint16_t reset_reason,
                          uint16_t param_version);
void bms_fuse_update(const bms_fuse_input_t *input,
                     const bms_measurement_t *measurement, uint32_t now_ms);
bms_fuse_state_t bms_fuse_get_state(void);
const bms_fuse_record_t *bms_fuse_get_record(void);
const bms_fuse_config_t *bms_fuse_get_config(void);

uint8_t bms_fuse_hw_is_supported(void);
uint8_t bms_fuse_hw_authorized(void);
uint8_t bms_fuse_hw_fired_feedback(void);
uint8_t bms_fuse_hw_driver_ok(void);
void bms_fuse_hw_drive(uint8_t active);
uint8_t bms_fuse_persist_load(bms_fuse_record_t *record);
uint8_t bms_fuse_persist_save(const bms_fuse_record_t *record);
