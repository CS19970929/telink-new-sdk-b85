#ifndef BMS_SELFTEST_TYPES_H_
#define BMS_SELFTEST_TYPES_H_

#include "tl_common.h"

typedef enum {
    BMS_SELFTEST_STATE_RESET = 0,
    BMS_SELFTEST_STATE_STARTUP,
    BMS_SELFTEST_STATE_RUNTIME,
    BMS_SELFTEST_STATE_FAIL_SAFE
} bms_selftest_state_t;

typedef enum {
    BMS_FAULT_NONE = 0,
    BMS_FAULT_CPU_REGISTER = 1,
    BMS_FAULT_CPU_FLAG = 2,
    BMS_FAULT_CONTROL_FLOW = 3,
    BMS_FAULT_FLASH_MANIFEST = 4,
    BMS_FAULT_FLASH_CRC = 5,
    BMS_FAULT_RAM = 6,
    BMS_FAULT_STACK = 7,
    BMS_FAULT_CLOCK = 8,
    BMS_FAULT_INTERRUPT_LOST = 9,
    BMS_FAULT_INTERRUPT_FREQUENT = 10,
    BMS_FAULT_WATCHDOG_HEARTBEAT = 11,
    BMS_FAULT_ADC = 12,
    BMS_FAULT_AFE_COMM = 13,
    BMS_FAULT_AFE_CONFIG = 14,
    BMS_FAULT_MOS_FEEDBACK = 15,
    BMS_FAULT_RETENTION = 16,
    BMS_FAULT_INTERNAL_DATA = 17
} bms_fault_reason_t;

typedef enum {
    BMS_SELFTEST_ITEM_NONE = 0,
    BMS_SELFTEST_ITEM_CPU,
    BMS_SELFTEST_ITEM_CONTROL_FLOW,
    BMS_SELFTEST_ITEM_FLASH,
    BMS_SELFTEST_ITEM_RAM,
    BMS_SELFTEST_ITEM_STACK,
    BMS_SELFTEST_ITEM_CLOCK,
    BMS_SELFTEST_ITEM_INTERRUPT,
    BMS_SELFTEST_ITEM_ADC,
    BMS_SELFTEST_ITEM_AFE,
    BMS_SELFTEST_ITEM_MOS
} bms_selftest_item_t;

typedef enum {
    BMS_HEARTBEAT_AFE = (1u << 0),
    BMS_HEARTBEAT_PROTECTION = (1u << 1),
    BMS_HEARTBEAT_MOS = (1u << 2),
    BMS_HEARTBEAT_SELFTEST = (1u << 3)
} bms_heartbeat_t;

typedef struct {
    u16 state;
    u16 startup_result;
    u16 runtime_result;
    u16 active_fault;
    u16 last_fatal_fault;
    u16 current_item;
    u16 flags;
    u16 runtime_cycles;
    u32 fault_mask;
    u32 expected_flash_crc;
    u32 actual_flash_crc;
    u32 flash_progress;
    u32 flash_total;
    u32 irq_count;
    u32 irq_delta;
    u32 stack_high_water;
    u32 heartbeat_seen;
    u32 heartbeat_missing;
} bms_selftest_diag_t;

#endif
