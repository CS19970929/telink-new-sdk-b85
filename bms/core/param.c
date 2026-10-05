/* 文件功能：应用参数加载、保存和启动安全门禁；通过各产品独立更新编号决定参数组的保留或更新。
 * bms/core/param.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "param.h"
#include "bms_error.h"
#include "bms_sw_protection.h"
#include "bms_config_store.h"
#include "bms_event_log.h"
#include "bms_product_config.h"
#include "bms_state_store.h"
#include "bms_factory_mode.h"
#include <string.h>


PARAM_T g_tParam;
static uint8_t s_protection_params_valid;
static uint8_t s_storage_startup_valid;

uint8_t bms_protection_params_valid(void)
{
    return s_protection_params_valid && s_storage_startup_valid;
}

static void param_fill_default(PARAM_T *param)
{
    bms_config_store_get_default_protect(&param->protect);
}

/* 加载并验证配置后发布运行参数；更新失败时保留启动安全门禁，不能通过普通保存绕过。 */
void LoadParam(void)
{

    s_protection_params_valid = 0u;
    bms_diag_boot_word(26u, DIAG_STARTED);

    if (!bms_config_store_init()) {
        bms_diag_boot_word(26u, DIAG_INVALID);
        param_fill_default(&g_tParam);
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }
    if (!bms_config_store_get_protect(&g_tParam.protect)) {
        param_fill_default(&g_tParam);
        if (!bms_config_store_set_protect(&g_tParam.protect)) {
            bms_diag_boot_word(26u, DIAG_SAVE);
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
            return;
        }
    }

    /* Communication writes are validated before SaveParam(). Validate loaded
     * Flash data as well, but do not replace unrelated customer parameters with
     * defaults merely because an old/corrupt record is detected. */
    if (!bms_sw_protection_validate_params(&g_tParam.protect)) {
        bms_diag_boot_word(26u, DIAG_INVALID);
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }
    bms_diag_boot_word(26u, DIAG_OK);
    s_protection_params_valid = 1u;
}

uint8_t SaveParam(void)
{
    if (!bms_sw_protection_validate_params(&g_tParam.protect)) {
        s_protection_params_valid = 0u;
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    if (!bms_config_store_set_protect(&g_tParam.protect)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    s_protection_params_valid = 1u;
    return 1u;
}

void bms_parameters_startup(void)
{
    /* A failed boot update remains inhibited until reboot/retry through this
     * startup path. A later communication SaveParam cannot clear this gate. */
    s_storage_startup_valid = 0u;
    if (!bms_config_store_validate_startup()) goto failed;
    if (!bms_state_store_init()) {
        bms_diag_upgrade(DIAG_UPGRADE_STATE, 0u); goto failed;
    }
    if (!bms_event_log_init()) {
        bms_diag_upgrade(DIAG_UPGRADE_EVENT, 0u); goto failed;
    }
    s_storage_startup_valid = 1u;
    bms_diag_upgrade(DIAG_UPGRADE_OK, 0u);
    return;
failed:
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
}

void bms_param_diag_poll(void)
{
    bms_diag_params(s_protection_params_valid, s_storage_startup_valid);
}
