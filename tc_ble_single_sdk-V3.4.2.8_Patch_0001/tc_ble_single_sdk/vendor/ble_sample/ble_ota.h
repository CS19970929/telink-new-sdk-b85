#ifndef BLE_OTA_H_
#define BLE_OTA_H_

#include "app_config.h"

#if (BLE_OTA_SERVER_ENABLE)
void app_enter_ota_mode(void);
void app_ota_end_result(int result);
#endif

#endif /* BLE_OTA_H_ */
