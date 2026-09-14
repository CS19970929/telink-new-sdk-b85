#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include <stdint.h>
#include "bms_afe_backend.h"

/*
 * Compile-time AFE boundary used by the BMS application.
 *
 * HS-D008 has one physical AFE (DVC1124-2). The SAFE backend keeps the
 * existing DVC driver as the device implementation and interposes a small
 * supervisor for communication-fault output inhibit, valid-snapshot release,
 * reinitialization cooldown, short-circuit latching and final FET arbitration.
 *
 * DVC implementation files include dvc1124*.h before this header; for them the
 * legacy bms_afe_* symbols are intentionally not renamed. Application code that
 * only includes bms_afe.h is rebound to the safe supervisor at compile time.
 */
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124_SAFE) && \
    !defined(DVC1124_H_) && !defined(DVC1124_CONFIG_STORE_H_)
#define bms_afe_init                       dvc1124_safe_bms_afe_init
#define bms_afe_sample                     dvc1124_safe_bms_afe_sample
#define bms_afe_sleep                      dvc1124_safe_bms_afe_sleep
#define bms_afe_apply_protection_config    dvc1124_safe_bms_afe_apply_protection_config
#define bms_afe_set_fets                   dvc1124_safe_bms_afe_set_fets
#define bms_afe_set_output_enabled         dvc1124_safe_bms_afe_set_output_enabled
#define bms_afe_get_aux_measurements       dvc1124_safe_bms_afe_get_aux_measurements
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
