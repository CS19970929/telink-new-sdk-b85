/*
 * 文件功能：SDK 功能、GPIO 与调试编译配置；量产门禁集中校验，
 * 板级差异仍以产品配置为准。
 * bms/platform/telink/app_config.h；实际编译归属见各产品 sources.txt。
 */
/********************************************************************************************************
 * @file    app_config.h
 *
 * @brief   BLE SDK 应用接口头文件。
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#pragma once


/*
 * 默认用于开发；正式发布传入 -DBMS_PRODUCTION_BUILD=1，
 * 使不安全的调试/测试选项在编译时失败。
 */
#ifndef BMS_PRODUCTION_BUILD
#define BMS_PRODUCTION_BUILD 0
#endif
#if (BMS_PRODUCTION_BUILD != 0) && (BMS_PRODUCTION_BUILD != 1)
#error "BMS_PRODUCTION_BUILD must be 0 or 1"
#endif

// 功能配置
#include "bms_debug_log_config.h"

#define BLE_APP_PM_ENABLE								1
#define PM_DEEPSLEEP_RETENTION_ENABLE					0
#ifndef TEST_CONN_CURRENT_ENABLE
#define TEST_CONN_CURRENT_ENABLE            			0 	// 测量连接电流时关闭 UI，
// 以测量纯连接功耗。
#endif
#define BLE_APP_SECURITY_ENABLE      					0	// ACL 从设备 SMP，
// 强烈建议启用。
#define BLE_OTA_SERVER_ENABLE							1

/*
 * Flash 保护：SDK 默认启用，最终量产应用必须启用；
 * 开发调试使用 Telink BDT 的 Unlock命令访问 Flash。SDK 示例仅供参考，
 * 应用固件大小、OTA 方案和数据区域不同，不能未经调整直接用于量产；须理解原理和方法，
 * 按实际应用实现合适的保护机制。
 */
#define APP_FLASH_PROTECTION_ENABLE						1

/* 量产应用必须检查电池电压，防止低电压下异常写入或擦除 Flash！ */
#define APP_BATT_CHECK_ENABLE							0

// 调试配置
#ifndef DEBUG_GPIO_ENABLE
#define DEBUG_GPIO_ENABLE                    (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef UART_PRINT_DEBUG_ENABLE
#define UART_PRINT_DEBUG_ENABLE              0
#endif
#ifndef APP_LOG_EN
#define APP_LOG_EN                           (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_SMP_LOG_EN
#define APP_SMP_LOG_EN                       0
#endif
#ifndef APP_KEY_LOG_EN
#define APP_KEY_LOG_EN                       (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_CONTR_EVENT_LOG_EN
#define APP_CONTR_EVENT_LOG_EN               (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_HOST_EVENT_LOG_EN
#define APP_HOST_EVENT_LOG_EN                (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_OTA_LOG_EN
#define APP_OTA_LOG_EN                       (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_FLASH_INIT_LOG_EN
#define APP_FLASH_INIT_LOG_EN                (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_FLASH_PROT_LOG_EN
#define APP_FLASH_PROT_LOG_EN                (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif
#ifndef APP_BATT_CHECK_LOG_EN
#define APP_BATT_CHECK_LOG_EN                (BMS_PRODUCTION_BUILD ? 0 : 1)
#endif

// OTA 稳定性
#define APP_OTA_PROCESS_TIMEOUT_S			180
#define APP_OTA_DATA_PACKET_TIMEOUT_S		15

// 示例板选择配置
#if (__PROJECT_8258_BLE_SAMPLE__)
	// 仅支持 BOARD_825X_EVK_C1T139A30 和 BOARD_825X_DONGLE_C1T139A3。
	#define BOARD_SELECT							BOARD_825X_EVK_C1T139A30
#elif (__PROJECT_8278_BLE_SAMPLE__)
	// 仅支持 BOARD_827X_EVK_C1T197A30 和 BOARD_827X_DONGLE_C1T201A3。
	#define BOARD_SELECT							BOARD_827X_EVK_C1T197A30
#elif (__PROJECT_TC321X_BLE_SAMPLE__)
	// 仅支持 BOARD_TC321X_EVK_C1T357A20。
	#define BOARD_SELECT							BOARD_TC321X_EVK_C1T357A20
#endif



// UI 配置
#define	UI_KEYBOARD_ENABLE								0
#define	UI_LED_ENABLE									0
#define	UI_BUTTON_ENABLE								0

#if (UI_KEYBOARD_ENABLE)
	#define			CR_VOL_UP				0xf0
	#define			CR_VOL_DN				0xf1

	/* 普通键盘键值映射。 */
	#define		KB_MAP_NORMAL	{	{CR_VOL_DN,		VK_1},	 \
									{CR_VOL_UP,		VK_2}, }

	#define		KB_MAP_NUM		KB_MAP_NORMAL
	#define		KB_MAP_FN		KB_MAP_NORMAL
#endif

// 深睡保留标志
#if (__PROJECT_TC321X_BLE_SAMPLE__)
	#define USED_DEEP_ANA_REG				PM_ANA_REG_WD_CLR_BUF1
#else
	#define USED_DEEP_ANA_REG               DEEP_ANA_REG0 // u8，可在深睡期间保留 8 位信
	// 息。
#endif
#define	LOW_BATT_FLG					    BIT(0) // 为 1 表示低电量。
#define CONN_DEEP_FLG	                    BIT(4) // 为 1 表示连接态深睡，
// 为 0 表示广播态深睡。



// 系统时钟配置
#define CLOCK_SYS_CLOCK_HZ  								16000000


// 看门狗
#define MODULE_WATCHDOG_ENABLE		1
#define WATCHDOG_INIT_TIMEOUT		2000  //ms


// 打印调试信息
#if (UART_PRINT_DEBUG_ENABLE)
	#define DEBUG_INFO_TX_PIN           	GPIO_PC2
	#define PULL_WAKEUP_SRC_PC2         	PM_PIN_PULLUP_10K
	#define PC2_OUTPUT_ENABLE         		1
	#define PC2_DATA_OUT                    1
#endif

#if TEST_CONN_CURRENT_ENABLE
	#if DEBUG_GPIO_ENABLE || UART_PRINT_DEBUG_ENABLE || UI_KEYBOARD_ENABLE || UI_LED_ENABLE || UI_BUTTON_ENABLE
		#error "If testing current, the above definitions must be disable!!!"
	#endif
#endif

#ifndef BMS_DIAG_BUILD_ID
#define BMS_DIAG_BUILD_ID 0u
#endif
#ifndef BMS_DIAG_BUILD_DIRTY
#define BMS_DIAG_BUILD_DIRTY 0
#endif

#if BMS_PRODUCTION_BUILD
	#if (BMS_DIAG_BUILD_ID == 0u)
		#error "Production build requires a nonzero Git diagnostic build ID"
	#endif
	#if BMS_DIAG_BUILD_DIRTY
		#error "Production build requires a clean Git worktree"
	#endif
	#ifdef __TEST_SOC__
		#error "Production build forbids __TEST_SOC__ command hooks"
	#endif
	#if TEST_CONN_CURRENT_ENABLE || DEBUG_GPIO_ENABLE || UART_PRINT_DEBUG_ENABLE
		#error "Production build forbids current-test, debug GPIO and UART debug output"
	#endif
	#if !APP_FLASH_PROTECTION_ENABLE
		#error "Production build requires SDK flash protection"
	#endif
	#if !MODULE_WATCHDOG_ENABLE
		#error "Production build requires watchdog"
	#endif
#endif

#include "vendor/common/default_config.h"
