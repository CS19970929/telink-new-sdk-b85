#pragma once

#include <stdint.h>

void bms_safety_init(uint32_t now_ms);
uint8_t bms_safety_start_afe(void);
void bms_safety_sample(uint8_t afe_valid, uint16_t battery_ntc_mv,
                       uint16_t mos_ntc_mv, uint32_t pack_adc_mv,
                       uint32_t now_ms);
void bms_safety_poll(uint32_t now_ms);
void bms_safety_prepare_sleep(uint32_t now_ms);
void bms_safety_note_wakeup(uint32_t now_ms);
uint8_t bms_safety_watchdog_allowed(uint32_t now_ms);
uint32_t bms_safety_now_ms(void);
void bms_safety_on_param_update(uint32_t now_ms);
uint8_t bms_safety_primary_fault_legacy(void);
