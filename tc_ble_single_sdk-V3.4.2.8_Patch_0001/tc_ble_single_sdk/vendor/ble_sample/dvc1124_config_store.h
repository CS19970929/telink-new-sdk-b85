#ifndef DVC1124_CONFIG_STORE_H_
#define DVC1124_CONFIG_STORE_H_

#include "dvc1124.h"

/*
 * Legacy filename retained to avoid unnecessary source-order churn.
 *
 * DVC1124 operating/board configuration is no longer persisted here.  It is
 * compile-time product policy from dvc1124_project_config.h / D008 profile and
 * is applied on every AFE initialization.
 *
 * Flash ownership is limited to real runtime parameters in their dedicated
 * modules:
 *   - g_tParam.protect / bms_cold_kv_store: software protection parameters
 *   - bms_afe_hw_profile: AFE hardware protection parameters
 */
#define DVC1124_FIXED_CONFIG_COMPILE_TIME 1u

/*
 * D008 shutdown/wake test helpers.
 *
 * These are deliberately AFE-local APIs.  They do not reset the common BMS
 * guard state and therefore are suitable for bench-testing the DVC1124
 * shutdown -> I2C-wake mechanism without calling bms_afe_init().
 */
uint8_t DVC1124_AFE_Shutdown(void);
uint8_t DVC1124_AFE_WakeupFromShutdown(void);

#endif /* DVC1124_CONFIG_STORE_H_ */
