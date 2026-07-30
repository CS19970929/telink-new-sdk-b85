#pragma once

#include <stdint.h>

typedef uint32_t bms_inhibit_mask_t;

enum {
    BMS_INHIBIT_INIT             = 1UL << 0,
    BMS_INHIBIT_AFE_COMM         = 1UL << 1,
    BMS_INHIBIT_AFE_CONFIG       = 1UL << 2,
    BMS_INHIBIT_SAMPLE_INVALID   = 1UL << 3,
    BMS_INHIBIT_CELL_OV          = 1UL << 4,
    BMS_INHIBIT_CELL_UV          = 1UL << 5,
    BMS_INHIBIT_PACK_OV          = 1UL << 6,
    BMS_INHIBIT_PACK_UV          = 1UL << 7,
    BMS_INHIBIT_CHG_OC           = 1UL << 8,
    BMS_INHIBIT_DSG_OC1          = 1UL << 9,
    BMS_INHIBIT_SHORT            = 1UL << 10,
    BMS_INHIBIT_CHG_OT           = 1UL << 11,
    BMS_INHIBIT_CHG_UT           = 1UL << 12,
    BMS_INHIBIT_DSG_OT           = 1UL << 13,
    BMS_INHIBIT_DSG_UT           = 1UL << 14,
    BMS_INHIBIT_MOS_OT           = 1UL << 15,
    BMS_INHIBIT_FET_OFF_FAILED   = 1UL << 16,
    BMS_INHIBIT_WDT_RESET        = 1UL << 17,
    BMS_INHIBIT_FUSE_STATE       = 1UL << 18,
    BMS_INHIBIT_FACTORY          = 1UL << 19,
    BMS_INHIBIT_PARAM_INVALID    = 1UL << 20,
    BMS_INHIBIT_DIAGNOSTIC       = 1UL << 21,
    BMS_INHIBIT_OTA              = 1UL << 22,
    BMS_INHIBIT_HW_UNVERIFIED    = 1UL << 23,
    BMS_INHIBIT_DSG_OC2          = 1UL << 24
};

/* Compatibility name for existing status consumers; new code uses OC1/OC2. */
#define BMS_INHIBIT_DSG_OC BMS_INHIBIT_DSG_OC1

typedef struct {
    uint8_t charge_on;
    uint8_t discharge_on;
    uint8_t ctlc_on;
    uint8_t precharge_on;
} bms_actuator_state_t;

void bms_actuator_init(void);
void bms_set_charge_inhibit(uint32_t reason, uint8_t active);
void bms_set_discharge_inhibit(uint32_t reason, uint8_t active);
void bms_set_global_inhibit(uint32_t reason, uint8_t active);
uint32_t bms_get_charge_inhibit(void);
uint32_t bms_get_discharge_inhibit(void);
uint32_t bms_get_global_inhibit(void);
void bms_actuator_request_charge(uint8_t enable);
void bms_actuator_request_discharge(uint8_t enable);
void bms_actuator_request_precharge(uint8_t enable);
void bms_actuator_set_safety_ready(uint8_t ready);
void bms_actuator_update(uint32_t now_ms);
const bms_actuator_state_t *bms_actuator_get_state(void);

/* Implemented only by the hardware adapter (or by the host test stub). */
uint8_t bms_actuator_hw_apply(const bms_actuator_state_t *state);
