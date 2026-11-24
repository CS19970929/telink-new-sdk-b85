/*
 * 文件功能：BLE OTA 工作状态、连接参数与升级结果处理；
 * 升级期间配合原有 Flash/低功耗互锁。
 * bms/platform/telink/ble_ota.c；实际编译归属见各产品 sources.txt。
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "ble_ota.h"
#include "bms_serial_ota.h"
#include "flash_store_cfg.h"


#if (BLE_OTA_SERVER_ENABLE)
int app_ble_ota_write(void *packet)
{
    const rf_packet_att_write_t *p = packet;
    const u8 *value;
    u16 length, command;

    if (!p || bms_serial_ota_busy() || !flash_store_cfg_layout_supported() || p->l2capLen < 5u)
        return 0;
    length = (u16)(p->l2capLen - 3u);
    value = (const u8 *)&p->value;
    command = (u16)(value[0] | ((u16)value[1] << 8));
    if (!ota_is_working &&
        ((command == CMD_OTA_START && length == sizeof(ota_start_t)) ||
         (command == CMD_OTA_START_EXT && length == sizeof(ota_startExt_t))))
        app_storage_before_ota();
    return otaWrite(packet);
}
/* 进入 OTA 状态并切换升级所需连接与电源设置。 */
void app_enter_ota_mode(void)
{
    tlkapi_send_string_data(APP_OTA_LOG_EN, "[APP][OTA] enter ota mode", 0, 0);

    ota_is_working = 1;
    app_ble_request_ota_conn_param();

#if (BLE_APP_PM_ENABLE)
    bls_pm_setManualLatency(0);
#endif
}

/* 处理 OTA 结束结果并执行现有恢复或重启策略。 */
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
