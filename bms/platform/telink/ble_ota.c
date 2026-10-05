/* 文件功能：BLE OTA 工作状态、连接参数与升级结果处理；升级期间配合原有 Flash/低功耗互锁。
 * bms/platform/telink/ble_ota.c；实际编译归属见各产品 sources.txt。
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "ble_ota.h"

extern u32 latest_user_event_tick;

#if (BLE_OTA_SERVER_ENABLE)
void app_enter_ota_mode(void)
{
    tlkapi_send_string_data(APP_OTA_LOG_EN, "[APP][OTA] enter ota mode", 0, 0);

    ota_is_working = 1;
    latest_user_event_tick = clock_time();
    app_ble_request_ota_conn_param();

#if (BLE_APP_PM_ENABLE)
    bls_pm_setManualLatency(0);
#endif
}

void app_ota_end_result(int result)
{
    tlkapi_printf(APP_OTA_LOG_EN, "[APP][OTA] end result %d\n", result);

    if (result != OTA_SUCCESS)
    {
        ota_is_working = 0;
        app_ble_restore_normal_power();
    }
}
#endif
