#pragma once

#include <stdint.h>

typedef enum {
    BMS_AFE_OK = 0,
    BMS_AFE_ERR_READY,
    BMS_AFE_ERR_RESET,
    BMS_AFE_ERR_CONFIG_WRITE,
    BMS_AFE_ERR_CONFIG_VERIFY,
    BMS_AFE_ERR_COMM_CRC,
    BMS_AFE_ERR_SAMPLE_TIMEOUT,
    BMS_AFE_ERR_MTP
} bms_afe_error_t;

void bms_afe_init(void);
bms_afe_error_t bms_afe_startup(void);
void bms_afe_note_frame(uint8_t valid, uint32_t now_ms);
void bms_afe_alarm_isr(void);
void bms_afe_require_reconfigure(void);
void bms_afe_poll(uint32_t now_ms);
uint8_t bms_afe_take_revalidation_request(void);
uint8_t bms_afe_config_valid(void);
uint8_t bms_afe_frame_valid(void);
uint8_t bms_afe_charge_fet_on(void);
uint8_t bms_afe_discharge_fet_on(void);
bms_afe_error_t bms_afe_last_error(void);
