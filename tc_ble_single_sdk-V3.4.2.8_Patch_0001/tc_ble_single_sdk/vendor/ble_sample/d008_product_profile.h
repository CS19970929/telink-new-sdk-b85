#ifndef D008_PRODUCT_PROFILE_H_
#define D008_PRODUCT_PROFILE_H_

#include "bms_soc_defs.h"

/*
 * HS-D008 physical product assembly profile.
 *
 * This file intentionally owns only assembly identity that varies with the
 * selected battery build: physical series count and chemistry/SOC profile.
 *
 * Fixed DVC1124 board/fail-safe policy lives in dvc1124_project_config.h.
 * Runtime/persistent protection parameters remain in their dedicated stores:
 *   - software protection: g_tParam.protect
 *   - AFE hardware protection: bms_afe_hw_profile_t
 */
#define D008_PRODUCT_PROFILE_24S_LFP  1u
#define D008_PRODUCT_PROFILE_20S_NMC  2u

#ifndef D008_PRODUCT_PROFILE
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_24S_LFP
#endif

#if (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_24S_LFP)
#define D008_PRODUCT_CELL_COUNT       16u
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

#endif /* D008_PRODUCT_PROFILE_H_ */
