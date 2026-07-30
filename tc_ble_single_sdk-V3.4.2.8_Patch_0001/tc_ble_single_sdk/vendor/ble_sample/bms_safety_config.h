#pragma once

/*
 * Hardware-dependent safety features stay disabled until the schematic and
 * bench tests listed in BMS_SAFETY_IMPLEMENTATION.md have been completed.
 */
#ifndef BMS_POWER_PATH_HW_VERIFIED_ENABLE
#define BMS_POWER_PATH_HW_VERIFIED_ENABLE 0
#endif

#ifndef BMS_AFE_ALARM_ENABLE
#define BMS_AFE_ALARM_ENABLE 0
#endif

#ifndef BMS_AFE_RUNTIME_CONFIG_CHECK_ENABLE
#define BMS_AFE_RUNTIME_CONFIG_CHECK_ENABLE 1
#endif

/* Existing project value; polarity/mode must still be confirmed on hardware. */
#ifndef BMS_AFE_CTLC_MODE
#define BMS_AFE_CTLC_MODE 3u
#endif

#ifndef BMS_AFE_WATCHDOG_VERIFIED_ENABLE
#define BMS_AFE_WATCHDOG_VERIFIED_ENABLE 0
#endif

#ifndef BMS_AFE_PF_ENABLE
#define BMS_AFE_PF_ENABLE 0
#endif

#ifndef BMS_PRECHARGE_HW_ENABLE
#define BMS_PRECHARGE_HW_ENABLE 0
#endif

#ifndef BMS_FUSE_AUTO_TRIGGER_ENABLE
#define BMS_FUSE_AUTO_TRIGGER_ENABLE 0
#endif

#ifndef BMS_FUSE_HW_FEEDBACK_ENABLE
#define BMS_FUSE_HW_FEEDBACK_ENABLE 0
#endif

#ifndef BMS_FUSE_FACTORY_TEST_ENABLE
#define BMS_FUSE_FACTORY_TEST_ENABLE 0
#endif

/* Unknown until the fuse maker and driver circuit are verified: safe value. */
#ifndef BMS_FUSE_MAX_PULSE_MS
#define BMS_FUSE_MAX_PULSE_MS 0u
#endif

#define BMS_CELL_MAX                    16u
#define BMS_REQUIRED_VALID_FRAMES       3u
#define BMS_SAMPLE_STALE_MS             2500u
#define BMS_CONFIG_CHECK_PERIOD_MS      10000u
#define BMS_FET_OFF_GRACE_MS            500u
#define BMS_FET_OFF_CONFIRM_FRAMES      3u
#define BMS_FET_DANGER_CURRENT_MA       2000
#define BMS_FUSE_SEVERE_HOLD_MS         15000u
#define BMS_RECOVERY_GUARD_MS           2000u

#if (BMS_AFE_CTLC_MODE > 3u)
#error "BMS_AFE_CTLC_MODE must fit the SH367309 two-bit field"
#endif
