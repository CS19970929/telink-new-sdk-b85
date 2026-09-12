#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include <stdint.h>

/*
 * Compile-time AFE boundary used by the BMS application.
 *
 * A product build contains one AFE adapter, so a runtime ops table would only
 * add indirection and code size.  A new AFE implements this small interface;
 * app.c and the BMS core do not include device-register APIs.
 */
void bms_afe_init(void);
void bms_afe_sample(void);
void bms_afe_sleep(void);
uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on);
void bms_afe_set_output_enabled(uint8_t enabled);

typedef struct
{
    uint16_t battery_ntc_mv;
    uint16_t mos_ntc_mv;
    uint32_t battery_ntc_100ohm;
    uint32_t mos_ntc_100ohm;
    uint32_t pack_voltage_mv;
} bms_afe_aux_measurements_t;

/* Returns 1 for a valid snapshot; a failed snapshot is returned as all zeros. */
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurements);

#endif /* BMS_AFE_H_ */
