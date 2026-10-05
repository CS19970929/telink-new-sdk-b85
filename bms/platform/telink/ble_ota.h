/*
 * 文件功能：BLE OTA 工作状态、连接参数与升级结果处理；
 * 升级期间配合原有 Flash/低功耗互锁。
 * bms/platform/telink/ble_ota.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BLE_OTA_H_
#define BLE_OTA_H_

#include "app_config.h"

#if (BLE_OTA_SERVER_ENABLE)
/* 进入 OTA 状态并切换升级所需连接与电源设置。 */
void app_enter_ota_mode(void);
/* 处理 OTA 结束结果并执行现有恢复或重启策略。 */
void app_ota_end_result(int result);
#endif

#endif /* 条件编译结束： BLE_OTA_H_ */
