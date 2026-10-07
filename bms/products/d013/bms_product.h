/* D013 产品入口：身份、板级输入、容量及通信；数值保持原产品配置。 */
#pragma once
#include "bms_soc_defs.h"

/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
#define BMS_PRODUCT_ID 13u
#define BMS_AFE_BACKEND 2
#define BMS_PRODUCT_CHEMISTRY BMS_SOC_CHEMISTRY_AUTO
#define BMS_PRODUCT_SOC_PROFILE_ID BMS_SOC_PROFILE_AUTO

/* D013 专属原理图/BOM 尚缺，保持未签核。 */
#define BMS_D013_HW_CONFIG_APPROVED 0

/* D013：4S / 100 uOhm；无 balance/heater/MOS NTC，板级映射待实板确认。 */
#define SH3673510_BOARD_CELL_COUNT               4u
#define SH3673510_BOARD_SHUNT_UOHM              100u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      0u /* 待验证硬件。 */
#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u /* 待验证硬件。 */
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED 0u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 0u

#include "../sh3673510_defaults.h"

/* 文件功能：产品容量、名称、采样及通信功能配置。 */

/* 量产选择：D013 固定直连 UART Modbus RTU，不使用 SIF/单线。 */
#define _UL_RENZHENG_ENABLE_
#define _FUNC_UART_
#define MODBUS_RS485_ENABLE              0
#ifndef FAC_TEST
#define _DI_SWITCH_SYS_ONOFF
#endif

#define __SLEEP_VNORMAL__             (3000)
#define __SLEEP_TIMENORMAL__          (60 * 60 * 24)
#define __SLEEP_VLOW__                (2800)
#define __SLEEP_TIMEVLOW__            (60 * 60 * 1)

/* 保持历史数字线协议产品 ID；存储有独立标记。 */
#define FD_BMS_TYPE 2u
#define SeriesNum                      SH3673510_BOARD_CELL_COUNT
/* D013 开发容量 11.6 Ah（0.1 Ah 单位）；需产品参数签核。 */
#define CapacityFactory                116
#define BMS_HARDWARE_VERDION_DEFAULT   "D013"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.0"
#define BMS_SERIAL_NUMBER_DEFAULT      "D013-UNSET"


#define DEV_NAME_STR  "BT_D013"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)
#define DEV_NAME_STR2 "BT_D013_FACTORY"
#define DEV_NAME_LEN2 (sizeof(DEV_NAME_STR2)-1)


#define BMS_DEFAULT_CUV3_MV 3000u
#define BMS_DEFAULT_CUV3_FILTER 1000u
