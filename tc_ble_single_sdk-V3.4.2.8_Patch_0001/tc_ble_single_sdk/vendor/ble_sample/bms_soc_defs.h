#pragma once

/* Stable product-facing SOC identifiers. Keep numeric values backward compatible. */
#define BMS_SOC_CHEMISTRY_AUTO 0u
#define BMS_SOC_CHEMISTRY_LFP  1u
#define BMS_SOC_CHEMISTRY_NMC  2u

#define BMS_SOC_PROFILE_AUTO        0u
#define BMS_SOC_PROFILE_GENERIC_LFP 1u
#define BMS_SOC_PROFILE_GENERIC_NMC 2u

#define BMS_SOC_PROFILE_GENERIC_LFP_VERSION 2u
#define BMS_SOC_PROFILE_GENERIC_NMC_VERSION 2u

/* Protocol capacity fields are uint16 in 0.01 Ah; nominal/learning use 0.1 Ah.
 * floor(65535 / 10) also bounds existing 32-bit SOC percentage arithmetic. */
#define BMS_SOC_CAPACITY_MAX_0P1AH 6553u
