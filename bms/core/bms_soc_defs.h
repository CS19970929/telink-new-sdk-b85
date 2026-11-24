#pragma once

#define BMS_SOC_CHEMISTRY_LFP 1u
#define BMS_SOC_CHEMISTRY_NMC 2u

/* 额定容量以 0.1 Ah 配置，外部容量以 0.01 Ah 的 uint16_t 上报。 */
#define BMS_SOC_CAPACITY_MAX_0P1AH 6553u
#define BMS_SOC_UNITS_PER_0P1AH 3600u
