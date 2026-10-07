/*
 * 文件功能：应用参数加载、保存和启动安全门禁；通过各产品独立更新编号决定参数组的保留
 * 或更新。
 * bms/core/bms_parameters.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "bms_parameters.h"
#include "bms_error.h"
#include "bms_sw_protection.h"
#include "bms_config_store.h"
#include "bms_event_log.h"
#include "bms_state_store.h"

bms_protection_params_t g_bms_protection_params;
static uint8_t s_protection_params_valid;
static uint8_t s_storage_startup_valid;

/* 检查软件保护参数的阈值及恢复关系。 */
uint8_t bms_protection_params_valid(void)
{
    return s_protection_params_valid && s_storage_startup_valid;
}

/* 加载并验证配置后发布运行参数；更新失败时保留启动安全门禁，不能通过普通保存绕过。 */
static void parameters_load_protection(void)
{
    s_protection_params_valid = 0u;
    bms_diag_boot_word(26u, DIAG_STARTED);

    /* 启动资格入口已经尝试 Config；这里只读缓存，失败不重试 I/O 或写回默认。 */
    if (!bms_config_store_get_protect(&g_bms_protection_params)) {
        bms_diag_boot_word(26u, DIAG_INVALID);
        bms_config_store_get_default_protect(&g_bms_protection_params);
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }

    /*
     * 通信写入在 bms_protection_params_commit() 前校验；加载的 Flash 数据也须校验，
     * 但发现旧/损坏记录不能将无关客户参数全换默认。
     */
    if (!bms_sw_protection_validate_params(&g_bms_protection_params)) {
        bms_diag_boot_word(26u, DIAG_INVALID);
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return;
    }
    bms_diag_boot_word(26u, DIAG_OK);
    s_protection_params_valid = 1u;
}

/* 只有校验和持久保存都成功，私有候选才发布。 */
uint8_t bms_protection_params_commit(const bms_protection_params_t *candidate)
{
    if (!bms_sw_protection_validate_params(candidate)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    if (!bms_config_store_set_protect(candidate)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0u;
    }
    g_bms_protection_params = *candidate;
    s_protection_params_valid = 1u;
    return 1u;
}

/* 加载并验证各持久域，确定启动输出资格。 */
static void parameters_validate_storage_startup(void)
{
    uint8_t config_valid, state_valid, event_valid;
    /*
     * 启动更新失败持续阻断，直到重启/重走本启动路径；
     * 后续通信 bms_protection_params_commit 不能清此门禁。
     */
    s_storage_startup_valid = 0u;
    /* 各域独立加载；Config 失败也须保留健康 State 的 SOC/循环次数缓存。 */
    config_valid = bms_config_store_validate_startup() ? 1u : 0u;
    state_valid = bms_state_store_init() ? 1u : 0u;
    event_valid = bms_event_log_init() ? 1u : 0u;
    if (!config_valid) goto failed;
    if (!state_valid) {
        bms_diag_upgrade(DIAG_UPGRADE_STATE, 0u); goto failed;
    }
    if (!event_valid) {
        bms_diag_upgrade(DIAG_UPGRADE_EVENT, 0u); goto failed;
    }
    s_storage_startup_valid = 1u;
    bms_diag_upgrade(DIAG_UPGRADE_OK, 0u);
    return;
failed:
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
}

/* 只在启动调用；先确定持久域资格，再加载保护，失败门禁不由在线提交清除。 */
void bms_parameters_init(void)
{
    parameters_validate_storage_startup();
    parameters_load_protection();
}

/* 刷新业务参数与启动门禁诊断快照。 */
void bms_parameters_diag_poll(void)
{
    bms_diag_params(s_protection_params_valid, s_storage_startup_valid);
}
