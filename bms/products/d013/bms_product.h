/* D013 产品入口：身份、板级输入、容量及通信；数值保持原产品配置。 */
#pragma once
#include "bms_soc_defs.h"

/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
/* 产品整体签核：参数与实板验收完成后，经受控提交置 1。 */
#define BMS_PRODUCT_RELEASE_APPROVED 0
#define BMS_PRODUCT_ID 13u
#define BMS_AFE_BACKEND 2
/* 允许编译参数显式选择 NMC；默认维持现有 LFP 参数。 */
#ifndef BMS_PRODUCT_CHEMISTRY
#define BMS_PRODUCT_CHEMISTRY BMS_SOC_CHEMISTRY_LFP
#endif
#include "../bms_battery_defaults.h"

/* D013 专属原理图/BOM 尚缺，保持未签核。 */
#define BMS_D013_HW_CONFIG_APPROVED 0

/* D013：默认 10S / 100 uOhm；无 balance/heater，TS4 MOS NTC 为用户确认的 10K-3435。 */
#ifndef BMS_BUILD_CELL_COUNT
#define SH3673510_BOARD_CELL_COUNT               10u
#else
#define SH3673510_BOARD_CELL_COUNT BMS_BUILD_CELL_COUNT
#endif
#define SH3673510_BOARD_SHUNT_UOHM              100u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      0u /* 待验证硬件。 */
#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u /* 待验证硬件。 */
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED 0u
/* TS4 必需：无效阻断充放电，过温使用独立软件 MOS 阈值。 */
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 1u

#include "../sh3673510_defaults.h"

/* 文件功能：产品容量、名称、采样及通信功能配置。 */

/* 量产选择：D013 固定直连 UART Modbus RTU，不使用 SIF/单线。 */
#define BMS_PRODUCT_UART_ENABLE 1
#define BMS_PRODUCT_SIF_ENABLE 0
#define BMS_PRODUCT_RS485_ENABLE              0
/* 仅控制开关/ACC触发休眠；关闭不改变开关输入、MOS逻辑或其他休眠入口。 */
#ifndef BMS_PRODUCT_SWITCH_SLEEP_ENABLE
#define BMS_PRODUCT_SWITCH_SLEEP_ENABLE 1
#endif
#ifndef FAC_TEST
#define BMS_PRODUCT_SWITCH_ENABLE 1
#else
#define BMS_PRODUCT_SWITCH_ENABLE 0
#endif

#define BMS_SLEEP_NORMAL_CELL_MV             (3000)
#define BMS_SLEEP_NORMAL_SECONDS          (60 * 60 * 24)
#define BMS_SLEEP_LOW_CELL_MV                (2800)
#define BMS_SLEEP_LOW_SECONDS            (60 * 60 * 1)

/* 保持历史数字线协议产品 ID；存储有独立标记。 */
#define BMS_PRODUCT_WIRE_ID 2u
#define BMS_PRODUCT_CELL_COUNT                      SH3673510_BOARD_CELL_COUNT
/* D013 开发容量 11.6 Ah（0.1 Ah 单位）；需产品参数签核。 */
#ifndef BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH                116
#endif
#define BMS_PRODUCT_HW_VERSION   "D013"
#define BMS_PRODUCT_SW_VERSION   "V8.6"
#define BMS_PRODUCT_DEFAULT_SERIAL      "D013-20261018"


#define BMS_PRODUCT_BLE_NAME  "BT_D013"
#define BMS_PRODUCT_BLE_NAME_LENGTH  (sizeof(BMS_PRODUCT_BLE_NAME)-1)
#define BMS_PRODUCT_FACTORY_BLE_NAME "BT_D013_FACTORY"
#define BMS_PRODUCT_FACTORY_BLE_NAME_LENGTH (sizeof(BMS_PRODUCT_FACTORY_BLE_NAME)-1)


#define BMS_DEFAULT_CUV3_FILTER 1000u
