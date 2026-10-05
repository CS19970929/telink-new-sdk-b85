/* 文件功能：BLE OTA 工作状态、连接参数与升级结果处理；升级期间配合原有 Flash/低功耗互锁。
 * bms/platform/telink/ble_ota.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BLE_OTA_H_
#define BLE_OTA_H_

#include "app_config.h"

#if (BLE_OTA_SERVER_ENABLE)
void app_enter_ota_mode(void);
void app_ota_end_result(int result);
#endif

#endif /* BLE_OTA_H_ */
