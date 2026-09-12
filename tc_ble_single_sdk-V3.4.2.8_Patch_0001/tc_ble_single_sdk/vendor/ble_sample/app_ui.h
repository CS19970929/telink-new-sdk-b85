/********************************************************************************************************
 * @file    app_ui.h
 *
 * @brief   BLE OTA callback declarations retained under the historical app_ui name.
 *******************************************************************************************************/
#ifndef APP_UI_H_
#define APP_UI_H_

#include "app_config.h"

#if (BLE_OTA_SERVER_ENABLE)
void app_enter_ota_mode(void);
void app_ota_end_result(int result);
#endif

#endif /* APP_UI_H_ */
