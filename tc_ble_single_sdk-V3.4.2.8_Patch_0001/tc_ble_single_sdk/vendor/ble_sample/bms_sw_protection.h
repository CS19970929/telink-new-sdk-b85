#pragma once

#include <stdint.h>
#include "bms_measurement.h"

typedef enum {
    BMS_PROT_NORMAL = 0,
    BMS_PROT_ENTER_DELAY,
    BMS_PROT_ACTIVE,
    BMS_PROT_RECOVERY_DELAY,
    BMS_PROT_LATCHED
} bms_prot_state_t;

typedef enum {
    BMS_PROT_CELL_OV = 0,
    BMS_PROT_CELL_UV,
    BMS_PROT_PACK_OV,
    BMS_PROT_PACK_UV,
    BMS_PROT_CHG_OC,
    BMS_PROT_DSG_OC1,
    BMS_PROT_DSG_OC2,
    BMS_PROT_SHORT,
    BMS_PROT_CHG_OT,
    BMS_PROT_CHG_UT,
    BMS_PROT_DSG_OT,
    BMS_PROT_DSG_UT,
    BMS_PROT_MOS_OT,
    BMS_PROT_COUNT
} bms_protection_id_t;

typedef enum {
    BMS_SEVERITY_WARNING = 0,
    BMS_SEVERITY_PROTECTION,
    BMS_SEVERITY_GLOBAL_SHUTDOWN,
    BMS_SEVERITY_LATCHED,
    BMS_SEVERITY_FUSE_ELIGIBLE
} bms_fault_severity_t;

typedef struct {
    int32_t enter_value;
    int32_t recover_value;
    uint32_t enter_delay_ms;
    uint32_t recover_delay_ms;
    uint32_t inhibit_reason;
    uint16_t fault_code;
    uint8_t trip_when_high;
    uint8_t inhibit_charge;
    uint8_t inhibit_discharge;
    uint8_t global_shutdown;
    uint8_t auto_recover;
    uint8_t latch;
    uint8_t fet_verify;
    uint8_t fuse_eligible;
    uint8_t severity;
} bms_protection_cfg_t;

typedef struct {
    bms_prot_state_t state;
    uint32_t state_since_ms;
} bms_protection_runtime_t;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
    bms_protection_cfg_t item[BMS_PROT_COUNT];
} bms_sw_protection_config_t;

void bms_sw_protection_defaults(bms_sw_protection_config_t *cfg);
void bms_sw_protection_config_finalize(bms_sw_protection_config_t *cfg);
uint8_t bms_sw_protection_config_validate(const bms_sw_protection_config_t *cfg);
void bms_sw_protection_init(const bms_sw_protection_config_t *cfg);
void bms_sw_protection_update(const bms_measurement_t *m, uint32_t now_ms);
uint32_t bms_sw_protection_active_mask(void);
uint16_t bms_sw_protection_primary_fault(void);
const bms_protection_runtime_t *bms_sw_protection_runtime(bms_protection_id_t id);
const bms_sw_protection_config_t *bms_sw_protection_config(void);

/* Safety log adapter; it must never block an actuator update. */
void bms_safety_log_transition(uint16_t fault_code, uint8_t active,
                               const bms_measurement_t *measurement);
