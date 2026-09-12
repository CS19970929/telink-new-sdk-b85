/********************************************************************************************************
 * @file    app_ui.c
 *
 * @brief   Legacy file name retained temporarily for BLE OTA callbacks.
 *
 * The original Telink keyboard/button/HID sample UI code was removed from the
 * BMS template because UI_KEYBOARD_ENABLE and UI_BUTTON_ENABLE are disabled and
 * those demo paths are not part of the product firmware.  Keep only the OTA
 * callbacks that are registered by app.c.  A later refactor can rename this
 * module to ble_ota.c once the application source layout is reorganized.
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "app_ui.h"

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
