#ifndef D008_PRODUCT_PROFILE_H_
#define D008_PRODUCT_PROFILE_H_

#include "bms_soc_defs.h"

/*
 * HS-D008 physical product assembly profile.
 *
 * This file intentionally contains only facts that differ with the assembled
 * battery topology/chemistry and are already supported by the D008 schematic:
 *  - 24S LFP
 *  - 20S NMC
 *
 * Capacity, current limits, OV/UV values, temperature limits and other product
 * policy remain in the existing parameter store until separately signed off.
 * Selecting 20S NMC therefore does NOT claim that the historical protection
 * defaults are production-ready for NMC.
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

#endif /* D008_PRODUCT_PROFILE_H_ */
