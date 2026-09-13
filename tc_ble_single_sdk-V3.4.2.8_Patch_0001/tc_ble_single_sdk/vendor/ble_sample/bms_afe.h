#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include <stdint.h>
#include "bms_afe_backend.h"

/*
 * Compile-time AFE boundary used by the BMS application.
 *
 * A product build contains one active AFE adapter. Legacy DVC1124 sources are
 * still compiled for reuse/contract checks, but application calls are rebound
 * to the SH3673510 adapter on the HS-D011 profile. The DVC implementation
 * includes dvc1124*.h before this header, so its legacy symbols are not renamed.
 */
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510) && \
    !defined(DVC1124_H_) && !defined(DVC1124_CONFIG_STORE_H_)
#define bms_afe_init                       sh3673510_bms_afe_init
#define bms_afe_sample                     sh3673510_bms_afe_sample
#define bms_afe_sleep                      sh3673510_bms_afe_sleep
#define bms_afe_apply_protection_config    sh3673510_bms_afe_apply_protection_config
#define bms_afe_set_fets                   sh3673510_bms_afe_set_fets
#define bms_afe_set_output_enabled         sh3673510_bms_afe_set_output_enabled
#define bms_afe_get_aux_measurements       sh3673510_bms_afe_get_aux_measurements
#endif

void bms_afe_init(void);
void bms_afe_sample(void);
void bms_afe_sleep(void);
uint8_t bms_afe_apply_protection_config(void);
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
