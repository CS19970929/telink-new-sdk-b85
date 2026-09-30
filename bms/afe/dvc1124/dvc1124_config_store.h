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

/* Backend-only primitive. Product/test code must use the guarded
 * bms_afe_test_enter_shutdown()/bms_afe_test_wake() lifecycle. */
uint8_t dvc1124_backend_enter_shutdown(void);

#endif /* DVC1124_CONFIG_STORE_H_ */
