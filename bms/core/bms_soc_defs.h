/*
 * 文件功能：SOC 配置、持久状态与容量学习的共享数据定义；
 * 字段单位和版本约束供算法与存储共同使用。
 * bms/core/bms_soc_defs.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

/* 稳定的产品 SOC 标识，数值保持向后兼容。 */
#define BMS_SOC_CHEMISTRY_AUTO 0u
#define BMS_SOC_CHEMISTRY_LFP  1u
#define BMS_SOC_CHEMISTRY_NMC  2u

#define BMS_SOC_PROFILE_AUTO        0u
#define BMS_SOC_PROFILE_GENERIC_LFP 1u
#define BMS_SOC_PROFILE_GENERIC_NMC 2u

#define BMS_SOC_PROFILE_GENERIC_LFP_VERSION 2u
#define BMS_SOC_PROFILE_GENERIC_NMC_VERSION 2u

/*
 * 协议容量为 uint16、单位 0.01 Ah；名义/学习容量为 0.1 Ah。
 * floor(65535/10) 也限制既有 32 位 SOC 百分比运算范围。
 */
#define BMS_SOC_CAPACITY_MAX_0P1AH 6553u
