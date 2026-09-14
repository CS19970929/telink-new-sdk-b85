#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include <stdint.h>
#include "bms_afe_backend.h"

/*
 * Stable compile-time AFE boundary used by the BMS application.
 *
 * Application/business/transport code calls only bms_afe_*.
 * Device backends are rebound while their implementation sources are compiled,
 * then bms_afe_guard.c owns the public entry points. This keeps one product AFE
 * at build time without a runtime ops table and gives every backend the same
 * communication-health/output-inhibit policy.
 */

/* DVC implementation sources include dvc1124*.h before this header. */
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124) && \
    (defined(DVC1124_H_) || defined(DVC1124_CONFIG_STORE_H_))
#define bms_afe_init                       dvc1124_backend_init
#define bms_afe_sample                     dvc1124_backend_sample
#define bms_afe_sleep                      dvc1124_backend_sleep
#define bms_afe_apply_protection_config    dvc1124_backend_apply_protection_config
#define bms_afe_set_fets                   dvc1124_backend_set_fets
#define bms_afe_set_output_enabled         dvc1124_backend_set_output_enabled
#define bms_afe_get_aux_measurements       dvc1124_backend_get_aux_measurements
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

/* Backend-private symbols used only by bms_afe_guard.c and backend sources. */
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
void dvc1124_backend_init(void);
void dvc1124_backend_sample(void);
void dvc1124_backend_sleep(void);
uint8_t dvc1124_backend_apply_protection_config(void);
uint8_t dvc1124_backend_set_fets(uint8_t charge_on, uint8_t discharge_on);
void dvc1124_backend_set_output_enabled(uint8_t enabled);
uint8_t dvc1124_backend_get_aux_measurements(bms_afe_aux_measurements_t *measurements);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
void sh3673510_bms_afe_init(void);
void sh3673510_bms_afe_sample(void);
void sh3673510_bms_afe_sleep(void);
uint8_t sh3673510_bms_afe_apply_protection_config(void);
uint8_t sh3673510_bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on);
void sh3673510_bms_afe_set_output_enabled(uint8_t enabled);
uint8_t sh3673510_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurements);
#else
#error "Unsupported BMS_AFE_BACKEND"
#endif

#endif /* BMS_AFE_H_ */
