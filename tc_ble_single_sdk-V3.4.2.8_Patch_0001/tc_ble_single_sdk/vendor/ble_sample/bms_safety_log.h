#pragma once

#include <stdint.h>
#include "bms_measurement.h"

enum {
    BMS_SLOG_AFE_READY_FAIL = 0x2001,
    BMS_SLOG_AFE_RESET_FAIL,
    BMS_SLOG_AFE_CONFIG_FAIL,
    BMS_SLOG_AFE_CONFIG_DRIFT,
    BMS_SLOG_AFE_COMM_FAIL,
    BMS_SLOG_SAMPLE_INVALID,
    BMS_SLOG_FET_OFF_REQUEST,
    BMS_SLOG_FET_OFF_OK,
    BMS_SLOG_FET_OFF_FAILED,
    BMS_SLOG_WDT_RESET,
    BMS_SLOG_RESET_SUSPECT,
    BMS_SLOG_FUSE_ARMED,
    BMS_SLOG_FUSE_TRIGGER,
    BMS_SLOG_FUSE_FIRED,
    BMS_SLOG_FUSE_FAILED,
    BMS_SLOG_PARAM_CRC,
    BMS_SLOG_BOARD_CONFIG,
    BMS_SLOG_CTLC_STATE
};

void bms_safety_log_init(void);
void bms_safety_log_enqueue(uint16_t event, uint16_t flags,
                            const bms_measurement_t *measurement,
                            uint32_t now_ms);
void bms_safety_log_poll(void);
uint32_t bms_safety_log_drop_count(void);
