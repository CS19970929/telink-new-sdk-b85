#pragma once

/* BLE application features. */
#define BLE_APP_PM_ENABLE                 1
#define PM_DEEPSLEEP_RETENTION_ENABLE     0
#define TEST_CONN_CURRENT_ENABLE          0
#define BLE_APP_SECURITY_ENABLE           0
#define BLE_OTA_SERVER_ENABLE             1

/* Production flash protection remains enabled. */
#define APP_FLASH_PROTECTION_ENABLE       1
#define APP_BATT_CHECK_ENABLE             0

/* Debug/log configuration. */
#define DEBUG_GPIO_ENABLE                 1
#define UART_PRINT_DEBUG_ENABLE           0
#define APP_LOG_EN                        1
#define APP_SMP_LOG_EN                    0
#define APP_CONTR_EVENT_LOG_EN            1
#define APP_HOST_EVENT_LOG_EN             1
#define APP_OTA_LOG_EN                    1
#define APP_FLASH_INIT_LOG_EN             1
#define APP_FLASH_PROT_LOG_EN             1
#define APP_BATT_CHECK_LOG_EN             1

/* OTA stability. */
#define APP_OTA_PROCESS_TIMEOUT_S         180
#define APP_OTA_DATA_PACKET_TIMEOUT_S     15

/* Telink SDK board identity. HS-D008 pin ownership is defined in conf.h. */
#if (__PROJECT_8258_BLE_SAMPLE__)
#define BOARD_SELECT BOARD_825X_EVK_C1T139A30
#elif (__PROJECT_8278_BLE_SAMPLE__)
#define BOARD_SELECT BOARD_827X_EVK_C1T197A30
#elif (__PROJECT_TC321X_BLE_SAMPLE__)
#define BOARD_SELECT BOARD_TC321X_EVK_C1T357A20
#endif

/* Legacy Telink sample UI is not part of the BMS product. */
#define UI_KEYBOARD_ENABLE 0
#define UI_LED_ENABLE      0
#define UI_BUTTON_ENABLE   0

#define CLOCK_SYS_CLOCK_HZ 16000000

#define MODULE_WATCHDOG_ENABLE 1
#define WATCHDOG_INIT_TIMEOUT  2000

#if (UART_PRINT_DEBUG_ENABLE)
#define DEBUG_INFO_TX_PIN       GPIO_PC2
#define PULL_WAKEUP_SRC_PC2     PM_PIN_PULLUP_10K
#define PC2_OUTPUT_ENABLE       1
#define PC2_DATA_OUT            1
#endif

#include "vendor/common/default_config.h"

/*
 * Do not redefine SDK gpio/adc/i2c/uart/pm APIs in this header. app_config.h is
 * parsed while Telink driver declarations are still being processed.
 *
 * The DVC compatibility aliases are enabled later from conf.h. The SH367309
 * legacy implementation unit must keep its original symbol names so it can
 * continue supplying the remaining shared legacy helpers during migration.
 */
#if !defined(SH367309_DATADEAL_H_)
#define DVC1124_AFE_PROJECT 1
#endif
