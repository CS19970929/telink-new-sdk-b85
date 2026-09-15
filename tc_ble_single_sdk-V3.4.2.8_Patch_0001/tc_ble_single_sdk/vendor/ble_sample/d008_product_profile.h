#ifndef D008_PRODUCT_PROFILE_H_
#define D008_PRODUCT_PROFILE_H_

#include "bms_soc_defs.h"

/*
 * HS-D008 physical product assembly profile.
 *
 * This file owns compile-time product facts and fixed fail-safe policy.  These
 * values are firmware policy, not customer parameters, and must not be restored
 * from historical Flash configuration.
 *
 * Protection thresholds remain runtime/persistent parameters:
 *   - software protection: g_tParam.protect
 *   - AFE hardware protection: bms_afe_hw_profile_t
 */
#define D008_PRODUCT_PROFILE_24S_LFP  1u
#define D008_PRODUCT_PROFILE_20S_NMC  2u

#ifndef D008_PRODUCT_PROFILE
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_24S_LFP
#endif

#if (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_24S_LFP)
#define D008_PRODUCT_CELL_COUNT       24u
#define D008_PRODUCT_CHEMISTRY        BMS_SOC_CHEMISTRY_LFP
#define D008_PRODUCT_SOC_PROFILE_ID   BMS_SOC_PROFILE_GENERIC_LFP
#define D008_PRODUCT_PROFILE_NAME     "D008-24S-LFP"
#elif (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_20S_NMC)
#define D008_PRODUCT_CELL_COUNT       20u
#define D008_PRODUCT_CHEMISTRY        BMS_SOC_CHEMISTRY_NMC
#define D008_PRODUCT_SOC_PROFILE_ID   BMS_SOC_PROFILE_GENERIC_NMC
#define D008_PRODUCT_PROFILE_NAME     "D008-20S-NMC"
#else
#error "Unsupported D008_PRODUCT_PROFILE"
#endif

#if ((D008_PRODUCT_CELL_COUNT < 4u) || (D008_PRODUCT_CELL_COUNT > 24u))
#error "D008 product cell count is outside DVC1124-2 range"
#endif

/*
 * Fixed DVC fail-safe policy.
 * DVC1124 project_config.h uses #ifndef for these symbols, so defining them
 * here makes the product profile authoritative without creating a second
 * runtime/Flash owner.
 *
 * 4 s is the shortest DVC1124-2 I2C watchdog period.  On timeout both CHG and
 * DSG autonomous-close sources are enabled (mask bit cleared by the driver).
 */
#define DVC1124_I2C_WATCHDOG_SECONDS     4u
#define DVC1124_I2C_TIMEOUT_CLOSE_CHG    1u
#define DVC1124_I2C_TIMEOUT_CLOSE_DSG    1u

#endif /* D008_PRODUCT_PROFILE_H_ */
