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
typedef struct { uint8_t valid; uint8_t cell_count; uint8_t battery_temp_valid; uint8_t heater_temp_valid; uint8_t mos_temp_valid; uint16_t battery_temp_min_x10; uint16_t battery_temp_max_x10; uint16_t heater_temp_x10; uint16_t mos_temp_x10; } bms_afe_feature_snapshot_t;
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

#endif
