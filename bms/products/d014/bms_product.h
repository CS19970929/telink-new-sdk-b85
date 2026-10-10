/* D014 产品入口：身份、板级输入、容量及通信；数值保持原产品配置。 */
#pragma once
#include "bms_soc_defs.h"

/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
/* 产品整体签核：参数与实板验收完成后，经受控提交置 1。 */
#define BMS_PRODUCT_RELEASE_APPROVED 0
#define BMS_PRODUCT_ID 14u
#define BMS_AFE_BACKEND 2
/* 允许编译参数显式选择 NMC；默认维持现有 LFP 参数。 */
#ifndef BMS_PRODUCT_CHEMISTRY
#define BMS_PRODUCT_CHEMISTRY BMS_SOC_CHEMISTRY_LFP
#endif
#include "../bms_battery_defaults.h"

/* 均衡调度：0 不增加数量限制；温度两项均为 0 关闭新增回差，原温度保护仍有效。
 * 温度使用 (摄氏度 + 40) * 10；启用时必须 0 < 恢复值 < 停止值 <= 1650。
 * 数量限制总使能位数，奇偶交替由 AFE 完成。 */
#ifndef BMS_BALANCE_MAX_CELLS
#define BMS_BALANCE_MAX_CELLS 0u
#endif
#ifndef BMS_BALANCE_TEMP_STOP_X10
#define BMS_BALANCE_TEMP_STOP_X10 0u
#endif
#ifndef BMS_BALANCE_TEMP_RESUME_X10
#define BMS_BALANCE_TEMP_RESUME_X10 0u
#endif


/* D014：8S / 667 uOhm；TS3 NC，实装 TS4 MOS 10K-3435（RN4 图纸差异）。 */
#ifndef BMS_BUILD_CELL_COUNT
#define SH3673510_BOARD_CELL_COUNT               8u
#else
#define SH3673510_BOARD_CELL_COUNT BMS_BUILD_CELL_COUNT
#endif
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
#define BMS_PRODUCT_UART_ENABLE 1
#define BMS_PRODUCT_SIF_ENABLE 0
#define BMS_PRODUCT_RS485_ENABLE              1
/* 仅控制开关/ACC触发休眠；关闭不改变开关输入、MOS逻辑或其他休眠入口。 */
#ifndef BMS_PRODUCT_SWITCH_SLEEP_ENABLE
#define BMS_PRODUCT_SWITCH_SLEEP_ENABLE 1
#endif
/* 仅实板 UART DMA/RS485 线路测试，量产默认禁止。 */
#ifndef BMS_RS485_TX_DIAG_ENABLE
#define BMS_RS485_TX_DIAG_ENABLE         0
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

/* 保持历史数字产品 ID，兼容协议/存储。 */
#define BMS_PRODUCT_WIRE_ID 2u
#define BMS_PRODUCT_CELL_COUNT                      SH3673510_BOARD_CELL_COUNT

/*
 * 不补充原理图缺失的产品要求；D014 参数签核前 BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 保持开发默认，
 * 保护仍由持久软件配置和独立 AFE 硬件配置负责。
 */
#ifndef BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH                116
#endif
#define BMS_PRODUCT_HW_VERSION   "D014"
#define BMS_PRODUCT_SW_VERSION   "V1.6"
#define BMS_PRODUCT_DEFAULT_SERIAL      "D014-20260925"

/*
 * 遗留 SOC/电流检测兼容字段；
 * SH3673510 电流换算直接用 SH3673510_BOARD_SHUNT_UOHM（667uOhm）。
 */

#define BMS_PRODUCT_BLE_NAME  "BT_D014"
#define BMS_PRODUCT_BLE_NAME_LENGTH  (sizeof(BMS_PRODUCT_BLE_NAME)-1)
#define BMS_PRODUCT_FACTORY_BLE_NAME "BT_D014_FACTORY"
#define BMS_PRODUCT_FACTORY_BLE_NAME_LENGTH (sizeof(BMS_PRODUCT_FACTORY_BLE_NAME)-1)


#define BMS_DEFAULT_CUV3_FILTER 1000u
