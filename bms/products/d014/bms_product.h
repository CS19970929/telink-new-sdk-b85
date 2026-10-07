/* D014 产品入口：身份、板级输入、容量及通信；数值保持原产品配置。 */
#pragma once
#include "bms_soc_defs.h"

/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
#define BMS_PRODUCT_ID 14u
#define BMS_AFE_BACKEND 2
#define BMS_PRODUCT_CHEMISTRY BMS_SOC_CHEMISTRY_AUTO
#define BMS_PRODUCT_SOC_PROFILE_ID BMS_SOC_PROFILE_AUTO

/* D014：8S / 667 uOhm；TS3 NC，实装 TS4 MOS 10K-3435（RN4 图纸差异）。 */
#define SH3673510_BOARD_CELL_COUNT               8u
#define SH3673510_BOARD_SHUNT_UOHM              667u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      1u
#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED   0u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 1u

#include "../sh3673510_defaults.h"

#if SH3673510_PRODUCT_HEATER_SUPPORTED || SH3673510_PRODUCT_HEATER_NTC_SUPPORTED
#error "D014 has no fitted heater or TS3 sensor"
#endif

/* 文件功能：产品容量、名称、采样及通信功能配置。 */

/* 量产选择：D014 固定隔离 RS485 Modbus RTU，不使用 SIF/单线。 */
#define _UL_RENZHENG_ENABLE_
#define _FUNC_UART_
#define MODBUS_RS485_ENABLE              1
/* 仅实板 UART DMA/RS485 线路测试，量产默认禁止。 */
#ifndef BMS_RS485_TX_DIAG_ENABLE
#define BMS_RS485_TX_DIAG_ENABLE         0
#endif
#ifndef FAC_TEST
#define _DI_SWITCH_SYS_ONOFF
#endif

#define __SLEEP_VNORMAL__             (3000)
#define __SLEEP_TIMENORMAL__          (60 * 60 * 24)
#define __SLEEP_VLOW__                (2800)
#define __SLEEP_TIMEVLOW__            (60 * 60 * 1)

/* 保持历史数字产品 ID，兼容协议/存储。 */
#define FD_BMS_TYPE 2u
#define SeriesNum                      SH3673510_BOARD_CELL_COUNT

/*
 * 不补充原理图缺失的产品要求；D014 参数签核前 CapacityFactory 保持开发默认，
 * 保护仍由持久软件配置和独立 AFE 硬件配置负责。
 */
#define CapacityFactory                116
#define BMS_HARDWARE_VERDION_DEFAULT   "D014"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.6"
#define BMS_SERIAL_NUMBER_DEFAULT      "D014-20260925"

/*
 * 遗留 SOC/电流检测兼容字段；
 * SH3673510 电流换算直接用 SH3673510_BOARD_SHUNT_UOHM（667uOhm）。
 */

#define DEV_NAME_STR  "BT_D014"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)
#define DEV_NAME_STR2 "BT_D014_FACTORY"
#define DEV_NAME_LEN2 (sizeof(DEV_NAME_STR2)-1)


#define BMS_DEFAULT_CUV3_MV 3000u
#define BMS_DEFAULT_CUV3_FILTER 1000u
