#ifndef BMS_HOST_BRIDGE_H
#define BMS_HOST_BRIDGE_H
#include <stdint.h>
#include "bms_sw_protection.h"

/* Fixed legacy units at the production seam: pack=10mV, current=100mA,
 * temperature=(degC+40)*10. Python validates physical input before conversion.
 * One process owns one instance of the existing singleton protection module. */
typedef struct {
    uint32_t now_ms;
    uint16_t cell_min_mv, cell_max_mv, pack_10mv, charge_a10, discharge_a10;
    uint16_t battery_min_x10, battery_max_x10, mos_x10;
    uint8_t battery_valid, mos_valid, communication_ok;
    uint8_t charge_request, discharge_request, output_enabled;
    uint8_t afe_charge_block, afe_discharge_block, short_active, physical_release;
} bms_host_input_t;

typedef struct {
    uint16_t fault[3];
    uint8_t charge_on, discharge_on, temp_break, short_latched, accepted;
} bms_host_command_t;

void bms_host_reset(void);
bms_host_command_t bms_host_step(const bms_host_input_t *input);
#endif
