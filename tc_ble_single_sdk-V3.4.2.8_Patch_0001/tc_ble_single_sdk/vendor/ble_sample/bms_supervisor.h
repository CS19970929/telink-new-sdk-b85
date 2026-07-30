#pragma once

#include <stdint.h>

typedef enum {
    BMS_START_RESET = 0,
    BMS_START_IO_SAFE,
    BMS_START_AFE_WAIT_READY,
    BMS_START_AFE_CONFIG,
    BMS_START_AFE_VERIFY,
    BMS_START_SAMPLE_VALIDATE,
    BMS_START_PROTECTION_CHECK,
    BMS_START_READY,
    BMS_START_LOCKED
} bms_start_state_t;

enum {
    BMS_TASK_AFE_SAMPLE = 1UL << 0,
    BMS_TASK_MCU_ADC = 1UL << 1,
    BMS_TASK_PROTECTION = 1UL << 2,
    BMS_TASK_DIAGNOSTICS = 1UL << 3,
    BMS_TASK_ACTUATOR = 1UL << 4,
    BMS_TASK_FET_MONITOR = 1UL << 5,
    BMS_TASK_FUSE = 1UL << 6,
    BMS_TASK_SAFETY_LOG = 1UL << 7
};

#define BMS_TASK_REQUIRED_MASK (BMS_TASK_AFE_SAMPLE | BMS_TASK_MCU_ADC | \
    BMS_TASK_PROTECTION | BMS_TASK_DIAGNOSTICS | BMS_TASK_ACTUATOR | \
    BMS_TASK_FET_MONITOR | BMS_TASK_FUSE | BMS_TASK_SAFETY_LOG)

void bms_supervisor_init(uint32_t now_ms, uint16_t reset_reason, uint8_t reset_suspicious);
void bms_supervisor_begin_afe_startup(void);
void bms_supervisor_note_afe_ready(uint8_t ok);
void bms_supervisor_note_afe_config(uint8_t ok);
void bms_supervisor_note_afe_verify(uint8_t ok);
void bms_supervisor_heartbeat(uint32_t task, uint32_t now_ms);
void bms_supervisor_update(uint32_t now_ms);
void bms_supervisor_require_revalidation(void);
uint8_t bms_supervisor_watchdog_allowed(uint32_t now_ms);
uint8_t bms_supervisor_safety_init_ok(void);
bms_start_state_t bms_supervisor_state(void);
uint16_t bms_supervisor_reset_reason(void);
