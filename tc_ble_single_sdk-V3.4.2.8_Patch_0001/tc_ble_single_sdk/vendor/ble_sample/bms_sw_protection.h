#ifndef BMS_SW_PROTECTION_H_
#define BMS_SW_PROTECTION_H_

#include <stdint.h>

/*
 * AFE-independent software protection input.
 * Temperatures use the existing firmware encoding: (degC + 40) * 10.
 * Voltage/current values are read from g_stCellInfoReport in the legacy units
 * documented by bms_state.h.
 */
typedef struct
{
    uint8_t battery_temp_valid;
    uint8_t mos_temp_valid;
    uint16_t battery_temp_min;
    uint16_t battery_temp_max;
    uint16_t mos_temp;
} bms_sw_protection_inputs_t;

void bms_sw_protection_init(void);
void bms_sw_protection_clear(void);
void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs);
void bms_sw_protection_record_fault_edges(void);
uint8_t bms_sw_protection_charge_blocked(void);
uint8_t bms_sw_protection_discharge_blocked(void);

#endif /* BMS_SW_PROTECTION_H_ */
