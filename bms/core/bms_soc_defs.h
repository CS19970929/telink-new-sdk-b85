/*
 * 文件功能：SOC 配置、持久状态与循环 SOH的共享数据定义；
 * 字段单位和版本约束供算法与存储共同使用。
 * bms/core/bms_soc_defs.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

/* 算法与模型的配套版本；旧 profile 可读，但不直接获得新电压校准资格。 */
#define BMS_SOC_POLICY_VERSION 3u
#define BMS_SOC_FULL_HOLD_MS 60000u
#define BMS_SOC_EMPTY_HOLD_MS 5000u
#define BMS_SOC_DISPLAY_STEP_MS 5000u
#define BMS_SOC_OCV_REST_MS 3600000u
#define BMS_SOC_OCV_CURRENT_MAX_MA 200u
#define BMS_SOC_OCV_DELTA_MAX_MV 200u
#define BMS_SOC_OCV_WINDOW_MINUTES 15u
#define BMS_SOC_OCV_DRIFT_MAX_MV 5u
#define BMS_SOC_OCV_POINT_MS 1800000u
#define BMS_SOC_LFP_PLATEAU_MIN_MV 3200u
#define BMS_SOC_LFP_PLATEAU_MAX_MV 3400u

/* 稳定的产品 SOC 标识，数值保持向后兼容。 */
#define BMS_SOC_CHEMISTRY_AUTO 0u
#define BMS_SOC_CHEMISTRY_LFP  1u
#define BMS_SOC_CHEMISTRY_NMC  2u

#define BMS_SOC_PROFILE_AUTO        0u
#define BMS_SOC_PROFILE_GENERIC_LFP 1u
#define BMS_SOC_PROFILE_GENERIC_NMC 2u

#define BMS_SOC_PROFILE_GENERIC_LFP_VERSION BMS_SOC_POLICY_VERSION
#define BMS_SOC_PROFILE_GENERIC_NMC_VERSION BMS_SOC_POLICY_VERSION

/*
 * 协议容量为 uint16、单位 0.01 Ah；名义容量为 0.1 Ah。
 * floor(65535/10) 也限制既有 32 位 SOC 百分比运算范围。
 */
#define BMS_SOC_CAPACITY_MAX_0P1AH 6553u
