/* 电池类型只选择编译默认值；运行参数仍由 Config 各分组独立持有。
 * 数值在各产品 dXXX_protection_config.h；这里保留类型校验及旧内部别名。 */
#pragma once

#include "bms_soc_defs.h"

#ifndef BMS_PRODUCT_CHEMISTRY
#error "Select BMS_PRODUCT_CHEMISTRY in the product header"
#endif

/* LFP 保留现有产品值；NMC 为已确认的 4.20 V 体系开发默认。 */
#if BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP
#ifndef BMS_BALANCE_START_VOLTAGE_MV_DEFAULT
#define BMS_BALANCE_START_VOLTAGE_MV_DEFAULT   3400u
#endif
#elif BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_NMC
#ifndef BMS_BALANCE_START_VOLTAGE_MV_DEFAULT
#define BMS_BALANCE_START_VOLTAGE_MV_DEFAULT   4100u
#endif
#else
/* 禁止 AUTO 或未知类型默默获得 LFP 保护默认。 */
#error "BMS_PRODUCT_CHEMISTRY must be LFP (1) or NMC (2)"
#endif

/* 压差不随化学体系变化；板级 capability 仍决定是否实际执行均衡。 */
#ifndef BMS_BALANCE_ENABLE_DEFAULT
#define BMS_BALANCE_ENABLE_DEFAULT            1u
#endif
#ifndef BMS_BALANCE_START_DELTA_MV_DEFAULT
#define BMS_BALANCE_START_DELTA_MV_DEFAULT     50u
#endif
#ifndef BMS_BALANCE_STOP_DELTA_MV_DEFAULT
#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT      30u
#endif
