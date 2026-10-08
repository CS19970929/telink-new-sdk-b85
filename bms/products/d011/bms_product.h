/* D011 产品入口：身份、板级输入、容量及通信；数值保持原产品配置。 */
#pragma once
#include "bms_soc_defs.h"

/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
/* 产品整体签核：参数与实板验收完成后，经受控提交置 1。 */
#define BMS_PRODUCT_RELEASE_APPROVED 0
#define BMS_PRODUCT_ID 11u
#define BMS_AFE_BACKEND 2
#define BMS_PRODUCT_CHEMISTRY BMS_SOC_CHEMISTRY_AUTO
#define BMS_PRODUCT_SOC_PROFILE_ID BMS_SOC_PROFILE_AUTO

/* D011：10S / 250 uOhm；TS3 加热、TS4 MOS 10K-3435，PB5 启动保持低。 */
#define SH3673510_BOARD_CELL_COUNT              10u
#define SH3673510_BOARD_SHUNT_UOHM              250u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      1u
#define SH3673510_PRODUCT_HEATER_SUPPORTED       1u
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED 1u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 1u

/* 不可逆加热熔断必须经独立状态机授权；启动保持无效电平。 */
#define BMS_BOARD_HEATER_CHG_PIN                     GPIO_PB4
#define BMS_BOARD_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5  /*
 * 不可逆加热熔断触发；独立熔断状态机验证并授权前保持低。
 */
#define BMS_BOARD_HEATER_FUSE_SAFE_LEVEL               0u

#include "../sh3673510_defaults.h"

/* 文件功能：产品容量、名称、采样及通信功能配置。 */

/* 量产选择：D011 固定 RS485 Modbus RTU，不使用 SIF/单线。 */
#define BMS_PRODUCT_UART_ENABLE 1
#define BMS_PRODUCT_UART_BAUD_RATE 115200u
#define BMS_PRODUCT_SIF_ENABLE 0
#define BMS_PRODUCT_RS485_ENABLE              1
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
/* 现有 D11 容量单位 Ah*10，是产品数据，不能由原理图推断。 */
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH                116
#define BMS_PRODUCT_HW_VERSION   "D011"
#define BMS_PRODUCT_SW_VERSION   "V4.8"
#define BMS_PRODUCT_DEFAULT_SERIAL      "D011-20261008"


#define BMS_PRODUCT_BLE_NAME  "BT_D011"
#define BMS_PRODUCT_BLE_NAME_LENGTH  (sizeof(BMS_PRODUCT_BLE_NAME)-1)
#define BMS_PRODUCT_FACTORY_BLE_NAME "BT_D011_FACTORY"
#define BMS_PRODUCT_FACTORY_BLE_NAME_LENGTH (sizeof(BMS_PRODUCT_FACTORY_BLE_NAME)-1)


#define BMS_DEFAULT_CUV3_MV 3000u
#define BMS_DEFAULT_CUV3_FILTER 1000u
