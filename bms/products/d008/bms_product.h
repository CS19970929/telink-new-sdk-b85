/* D008 产品入口：身份、板级输入、容量及通信；数值保持原产品配置。 */
#pragma once
#include "bms_soc_defs.h"

/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
/* 2026-10-08 按用户指示授权生产镜像生成；实板验收仍按硬件清单独立记录。 */
#define BMS_PRODUCT_RELEASE_APPROVED 1
#define BMS_PRODUCT_ID 8u
#define BMS_AFE_BACKEND 1
#define BMS_PRODUCT_CHEMISTRY D008_PRODUCT_CHEMISTRY
#define BMS_PRODUCT_SOC_PROFILE_ID D008_PRODUCT_SOC_PROFILE_ID
#include "d008_product_profile.h"

/* 本次授权接受现有 SCD 策略用于镜像生成；保留原阈值及 SCD 开关。 */
#define BMS_D008_SCD_POLICY_APPROVED 1
#define BMS_D008_20S_NMC_PROTECTION_APPROVED 0

#include "dvc1124_product_defaults.h"

/* 文件功能：产品容量、名称、采样及通信功能配置。 */


/*
 * 仅功耗对比：0 取消周期唤醒期限，采样依赖其他事件，
 * 可能错过 DVC Open-Wire COW 窗口；正常保护/SOC 时序保持 1，修改后重编译。
 */
#ifndef BMS_APP_SAMPLE_WAKEUP_ENABLE
#define BMS_APP_SAMPLE_WAKEUP_ENABLE 1u
#endif
#if (BMS_APP_SAMPLE_WAKEUP_ENABLE != 0u) && (BMS_APP_SAMPLE_WAKEUP_ENABLE != 1u)
#error "BMS_APP_SAMPLE_WAKEUP_ENABLE must be 0 or 1"
#endif


#define BMS_PRODUCT_SIF_ENABLE 1
#define BMS_PRODUCT_UART_ENABLE 1
#define BMS_PRODUCT_RS485_ENABLE 0
#define BMS_PRODUCT_SWITCH_ENABLE 0

/* D008 无独立开关；ACC-MCU 是预留接口，不是供电请求。 */

#define BMS_SLEEP_NORMAL_CELL_MV             	(3000)
#define	BMS_SLEEP_NORMAL_SECONDS	          (60 * 60 * 24)
#define BMS_SLEEP_LOW_CELL_MV     		          (2800)
#define	BMS_SLEEP_LOW_SECONDS		          (60 * 60 * 1)

/* 移除无关遗留产品表时保留持久化类型 ID 和出厂容量回退，容量单位 Ah * 10。 */
#define BMS_PRODUCT_WIRE_ID   12u
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 78u

#define  BMS_PRODUCT_SW_VERSION  	"V8.8"

/* 串数仅由 D008 装配 profile 选择，硬件版本为本产品固定身份。 */
#define BMS_PRODUCT_CELL_COUNT  (DVC1124_DEFAULT_CELL_COUNT)
#define BMS_PRODUCT_HW_VERSION "D008"

#define  BMS_PRODUCT_DEFAULT_SERIAL  	"D008-20261008"

#define BMS_PRODUCT_BLE_NAME  "BT_FD190126F03200046_007"
#define BMS_PRODUCT_BLE_NAME_LENGTH  (sizeof(BMS_PRODUCT_BLE_NAME)-1)

#define BMS_PRODUCT_FACTORY_BLE_NAME  "BT_FD260228F03200046_666"
#define BMS_PRODUCT_FACTORY_BLE_NAME_LENGTH  (sizeof(BMS_PRODUCT_FACTORY_BLE_NAME)-1)


/* HS-D008 原理图第 1 页：PD4 驱动 F1 支路；PB1 是负载检测，不是充电源。 */
#define BMS_BOARD_HEATER_FUSE_PIN    (GPIO_PD4)
#define BMS_BOARD_AFE_ENABLE_PIN     (GPIO_PD7)
#define BMS_BOARD_ACC_PIN            (GPIO_PA0)
#define BMS_BOARD_HEATER_ENABLE_PIN  (GPIO_PA1)
#define BMS_BOARD_LOAD_DETECT_PIN    (GPIO_PB1)
#define BMS_BOARD_OWC_TX_PIN          (GPIO_PC2)
#define BMS_BOARD_OWC_RX_PIN          (GPIO_PC3)
#define BMS_BOARD_MCU_LDO_PIN         (GPIO_PC4)
#define SOC25_PIN              (GPIO_PB4)
#define SOC50_PIN              (GPIO_PB5)
#define SOC75_PIN              (GPIO_PB7)
#define SOC100_PIN             (GPIO_PD3)
#define LED_BLUE_PIN           (GPIO_PB4)


#define BMS_DEFAULT_CUV3_MV 2200u
#define BMS_DEFAULT_CUV3_FILTER 100u
