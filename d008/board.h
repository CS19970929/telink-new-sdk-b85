#pragma once

/* 编译 SDK 驱动时同时生效，避免通用 EVK 的 GPIO 初值。 */
#define BOARD_SELECT 0xD008
#define CLOCK_SYS_CLOCK_HZ 16000000
#define BLE_APP_PM_ENABLE 1
#define PM_DEEPSLEEP_RETENTION_ENABLE 0
#define UART_PRINT_DEBUG_ENABLE 0
#define PC4_OUTPUT_ENABLE 1
#define PC4_DATA_OUT 1
#define PD7_OUTPUT_ENABLE 1
#define PD7_DATA_OUT 0
/* 旧 PCB 的 PA1、PD4 有外部执行电路，GPIO 初始态固定为低。 */
#define PA1_OUTPUT_ENABLE 1
#define PA1_DATA_OUT 0
#define PD4_OUTPUT_ENABLE 1
#define PD4_DATA_OUT 0
#define PA0_INPUT_ENABLE 1
#define PB1_INPUT_ENABLE 1
/* 负载检测的 Q40 拉低 PB1，释放后由弱上拉提供高电平。 */
#define PULL_WAKEUP_SRC_PB1 PM_PIN_PULLUP_1M
