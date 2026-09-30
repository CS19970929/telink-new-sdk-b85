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
