/*
 * 文件功能：应用调度与 BLE 电源策略；主循环按 200 ms 执行 AFE/SOC/MOS，
 * 协调通信、OTA 和休眠入口。
 * bms/app/app.h；实际编译归属见各产品 sources.txt。
 */
/*
 * **********************************************************************************
 * ********************
 * @file    app.h
 * @brief   Telink B85 的 BMS 应用与 BLE 生命周期接口。
 * **********************************************************************************
 * *******************
 */
#ifndef APP_H_
#define APP_H_

#include "conf.h"
#include "bms_error.h"

#define MY_DIRECT_ADV_TIME 2000000

#define MY_APP_ADV_CHANNEL BLT_ENABLE_ADV_ALL
#define MY_ADV_INTERVAL_MIN ADV_INTERVAL_800MS
#define MY_ADV_INTERVAL_MAX ADV_INTERVAL_800MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_500MS
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_500MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_30MS
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_30MS

#define MY_RF_POWER_INDEX RF_POWER_P3dBm

#define BLE_DEVICE_ADDRESS_TYPE BLE_DEVICE_ADDRESS_PUBLIC

void mos_update(void);

/* BLE/PM 既有保留状态；SDK 回调拥有连接状态，PM 消费时间和终止标志。 */
extern int device_in_connection_state;
extern u32 advertise_begin_tick;
extern u32 latest_user_event_tick;
extern u8 sendTerminate_before_enterDeep;
#if BMS_DEBUG_LOG_ENABLE
u32 app_ble_suspend_exit_count(void);
#endif
/* SDK 正常初始化完成后的业务启动与休眠回调。 */
void app_init(void);
void task_sleep_enter(u8 e, u8 *p, int n);

extern u8 ota_is_working;

/* 请求常规 BLE 连接参数。 */
void app_ble_request_normal_conn_param(void);
/* 请求 OTA 所需的 BLE 连接参数。 */
void app_ble_request_ota_conn_param(void);
/* 恢复常规 BLE 发射功率及连接设置。 */
void app_ble_restore_normal_power(void);

/* 上电或深睡唤醒时进行用户初始化。 */
void user_init_normal(void);

/* 深睡保留模式唤醒时进行用户初始化。 */
void user_init_deepRetn(void);

/* 应用协作主循环。 */
void main_loop(void);

/* 应用与 OTA 路径使用的 Flash 保护回调，地址区间为 [op_addr_begin, op_addr_end)。 */
void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end);
/* 判断 Flash 操作后是否需要恢复保护锁。 */
int app_flash_lock_restore_enabled(void);

#endif /* 头文件保护：APP_H_。 */
