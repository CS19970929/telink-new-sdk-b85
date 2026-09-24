#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include <stdint.h>
#include "bms_afe_backend.h"

/* Keep the AFE/public measurement convention intact. Only the SOC boundary
 * uses negative charge / positive discharge; SH reports the opposite sign. */
static inline int32_t bms_afe_current_to_soc_ma(int32_t current_ma)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    /* Unreachable for the SH ADC range, but avoid signed overflow on bad input. */
    if (current_ma == INT32_MIN) return INT32_MAX;
    return -current_ma;
#else
    return current_ma;
#endif
}



#if defined(DVC1124_H_) || defined(DVC1124_CONFIG_STORE_H_)
#define bms_afe_init                       dvc1124_backend_init
#define bms_afe_sample                     dvc1124_backend_sample
#define bms_afe_sleep                      dvc1124_backend_sleep
#define bms_afe_apply_protection_config    dvc1124_backend_apply_protection_config
#define bms_afe_set_fets                   dvc1124_backend_set_fets
#define bms_afe_set_output_enabled         dvc1124_backend_set_output_enabled
#define bms_afe_get_aux_measurements       dvc1124_backend_get_aux_measurements
#endif

void bms_afe_init(void); void bms_afe_sample(void);
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510) && !defined(bms_afe_sleep)
/* Failure keeps the MCU servicing the existing bounded AFE recovery path. */
uint8_t bms_afe_sleep(void);
#else
void bms_afe_sleep(void);
#endif
uint8_t bms_afe_apply_protection_config(void);
uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on);
void bms_afe_get_requested_fets(uint8_t *charge_on, uint8_t *discharge_on);
uint16_t bms_afe_get_guard_diagnostic_bits(void);
void bms_afe_set_output_enabled(uint8_t enabled);
/* Returns 0 only during the deliberate watchdog wait window. Diagnostic/raw
 * paths that bypass the normal guard must honor this gate and avoid AFE I/O. */
uint8_t bms_afe_bus_access_allowed(void);
typedef struct { uint16_t battery_ntc_mv; uint16_t mos_ntc_mv; uint32_t battery_ntc_100ohm; uint32_t mos_ntc_100ohm; uint32_t pack_voltage_mv; int32_t current_ma; uint32_t sample_tick_32k; } bms_afe_aux_measurements_t;
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurements);

#define BMS_AFE_FEATURE_MAX_CELLS 24u
typedef struct { uint8_t valid; uint8_t cell_count; uint8_t battery_temp_valid; uint8_t heater_temp_valid; uint8_t mos_temp_valid; uint8_t mos_temp_not_required; uint16_t battery_temp_min_x10; uint16_t battery_temp_max_x10; uint16_t heater_temp_x10; uint16_t mos_temp_x10; } bms_afe_feature_snapshot_t;
typedef enum { BMS_AFE_DIAG_IDLE=0u, BMS_AFE_DIAG_BUSY=1u, BMS_AFE_DIAG_READY=2u, BMS_AFE_DIAG_ERROR=3u } bms_afe_diag_state_t;
typedef struct { uint8_t valid; uint8_t determinate; uint8_t cell_count; uint32_t open_cell_mask; uint16_t diagnostic_cell_mv[BMS_AFE_FEATURE_MAX_CELLS]; } bms_afe_openwire_result_t;

uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *snapshot);
/* Returns 1 when this AFE/backend has a validated charger-presence detector;
 * returns 0 when the product must fall back to a board GPIO detector. */
uint8_t bms_afe_get_charge_source_present(uint8_t *present);
uint8_t bms_afe_set_balance_mask(uint32_t cell_mask);
uint8_t bms_afe_get_balance_mask(uint32_t *cell_mask);
uint8_t bms_afe_openwire_start(void);
bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *result);

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
void dvc1124_backend_init(void); void dvc1124_backend_sample(void); void dvc1124_backend_sleep(void);
uint8_t dvc1124_backend_apply_protection_config(void); uint8_t dvc1124_backend_set_fets(uint8_t,uint8_t); void dvc1124_backend_set_output_enabled(uint8_t); uint8_t dvc1124_backend_get_aux_measurements(bms_afe_aux_measurements_t *);
uint8_t dvc1124_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *); uint8_t dvc1124_backend_get_charge_source_present(uint8_t *); uint8_t dvc1124_backend_set_balance_mask(uint32_t); uint8_t dvc1124_backend_get_balance_mask(uint32_t *); uint8_t dvc1124_backend_openwire_start(void); bms_afe_diag_state_t dvc1124_backend_openwire_poll(bms_afe_openwire_result_t *);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
void sh3673510_bms_afe_init(void); void sh3673510_bms_afe_sample(void); uint8_t sh3673510_bms_afe_sleep(void);
uint8_t sh3673510_bms_afe_apply_protection_config(void); uint8_t sh3673510_bms_afe_set_fets(uint8_t,uint8_t); void sh3673510_bms_afe_set_output_enabled(uint8_t); uint8_t sh3673510_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *);
uint8_t sh3673510_bms_afe_get_fet_diagnostics(uint8_t *, uint8_t *, uint8_t *, uint8_t *);
typedef struct {
    uint8_t flag1, flag2, bstatus2;
    uint16_t backend_state, sensor_state;
    uint32_t charge_block_reasons, discharge_block_reasons;
} sh3673510_fet_diag_detail_t;
uint8_t sh3673510_bms_afe_get_fet_diag_detail(sh3673510_fet_diag_detail_t *detail);
uint8_t sh3673510_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *); uint8_t sh3673510_backend_get_charge_source_present(uint8_t *); uint8_t sh3673510_backend_set_balance_mask(uint32_t); uint8_t sh3673510_backend_get_balance_mask(uint32_t *); uint8_t sh3673510_backend_openwire_start(void); bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *);
#else
#error "Unsupported BMS_AFE_BACKEND"
#endif
#endif
