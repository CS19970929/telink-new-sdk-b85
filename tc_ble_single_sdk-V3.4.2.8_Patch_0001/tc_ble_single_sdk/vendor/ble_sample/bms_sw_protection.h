#ifndef BMS_SW_PROTECTION_H_
#define BMS_SW_PROTECTION_H_

#include <stdint.h>
#include "param.h"

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
    /* Set only when the product has no qualified MOS NTC protection input. */
    uint8_t mos_temp_not_required;
    uint16_t battery_temp_min;
    uint16_t battery_temp_max;
    uint16_t mos_temp;
} bms_sw_protection_inputs_t;

uint8_t bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *params);
void bms_sw_protection_init(void);
void bms_sw_protection_clear(void);
void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs);
/* Independent policy groups; disabled groups clear their owned state. */
void bms_sw_protection_update_groups(const bms_sw_protection_inputs_t *inputs,
                                     uint8_t voltage_current_enabled,
                                     uint8_t temperature_enabled);
void bms_sw_protection_record_fault_edges(void);
uint8_t bms_sw_protection_charge_blocked(void);
uint8_t bms_sw_protection_discharge_blocked(void);

#endif /* BMS_SW_PROTECTION_H_ */
