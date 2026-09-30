#ifndef BMS_AFE_DRIVER_H_
#define BMS_AFE_DRIVER_H_

/* Private driver declarations: guard, selected backend and read-only diagnostics.
 * Application code uses bms_afe.h and cannot bypass its communication gate. */
#include "bms_afe.h"

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
void dvc1124_backend_init(void);
void dvc1124_backend_sample(void);
uint8_t dvc1124_backend_sleep(void);
uint8_t dvc1124_backend_enter_shutdown(void);
uint8_t dvc1124_backend_apply_protection_config(void);
uint8_t dvc1124_backend_set_fets(uint8_t,uint8_t);
void dvc1124_backend_set_output_enabled(uint8_t);
uint8_t dvc1124_backend_get_aux_measurements(bms_afe_aux_measurements_t *);
uint8_t dvc1124_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *);
uint8_t dvc1124_backend_get_charge_source_present(uint8_t *);
uint8_t dvc1124_backend_set_balance_mask(uint32_t);
uint8_t dvc1124_backend_get_balance_mask(uint32_t *);
uint8_t dvc1124_backend_openwire_start(void);
bms_afe_diag_state_t dvc1124_backend_openwire_poll(bms_afe_openwire_result_t *);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
void sh3673510_bms_afe_init(void);
void sh3673510_bms_afe_sample(void);
uint8_t sh3673510_bms_afe_sleep(void);
uint8_t sh3673510_bms_afe_apply_protection_config(void);
uint8_t sh3673510_bms_afe_set_fets(uint8_t,uint8_t);
void sh3673510_bms_afe_set_output_enabled(uint8_t);
uint8_t sh3673510_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *);
uint8_t sh3673510_bms_afe_get_fet_diagnostics(uint8_t *, uint8_t *, uint8_t *, uint8_t *);
typedef struct {
    uint8_t flag1, flag2, bstatus2;
    uint16_t backend_state, sensor_state, mos_ntc_raw, mos_temp_x10;
    uint32_t mos_ntc_ohm;
    uint32_t charge_block_reasons, discharge_block_reasons;
} sh3673510_fet_diag_detail_t;
uint8_t sh3673510_bms_afe_get_fet_diag_detail(sh3673510_fet_diag_detail_t *detail);
uint8_t sh3673510_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *);
uint8_t sh3673510_backend_get_charge_source_present(uint8_t *);
uint8_t sh3673510_backend_set_balance_mask(uint32_t);
uint8_t sh3673510_backend_get_balance_mask(uint32_t *);
uint8_t sh3673510_backend_openwire_start(void);
bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *);
#else
#error "Unsupported BMS_AFE_BACKEND"
#endif

#endif /* BMS_AFE_DRIVER_H_ */
