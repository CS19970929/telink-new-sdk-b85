/* 文件功能：产品板级能力与 GPIO 操作；通过产品配置选择 heater/fuse/均衡路径，保持四产品硬件边界。
 * bms/platform/telink/bms_board.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_board.h"
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "bms_afe_backend.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
void bms_board_features_init(void)
{
    /* HS-D008: PA1 = MCC-EN-HT, PD4 = MCC-EN-RF. Keep both inactive at boot. */
    gpio_set_func(HEATER_EN_PIN, AS_GPIO);
    gpio_write(HEATER_EN_PIN, 0u);
    gpio_set_input_en(HEATER_EN_PIN, 0u);
    gpio_set_output_en(HEATER_EN_PIN, 1u);

    gpio_set_func(RF_EN_PIN, AS_GPIO);
    gpio_write(RF_EN_PIN, 0u);
    gpio_set_input_en(RF_EN_PIN, 0u);
    gpio_set_output_en(RF_EN_PIN, 1u);
}

uint8_t bms_board_charge_source_present(void)
{
    /* CHG-IN/PB1 is load detection, not proof of a charger. ACC and load
     * policy are intentionally unimplemented; do not authorize heating. */
    return 0u;
}

uint8_t bms_board_heater_supported(void)
{
    return 1u;
}

void bms_board_heater_set(uint8_t enabled)
{
    gpio_write(HEATER_EN_PIN, enabled ? 1u : 0u);
}

uint8_t bms_board_heater_fuse_supported(void)
{
    return 1u;
}

uint16_t bms_board_heater_off_fault_temp_x10(void)
{
    return (uint16_t)DVC1124_HEATER_OFF_FAULT_TEMP_X10;
}

uint16_t bms_board_heater_off_fault_confirm_ms(void)
{
    return (uint16_t)DVC1124_HEATER_OFF_FAULT_CONFIRM_MS;
}

void bms_board_heater_fuse_fire(void)
{
    /* HS-D008 irreversible heater-circuit fail-safe: PD4 / MCC-EN-RF high. */
    gpio_write(RF_EN_PIN, 1u);
}

uint8_t bms_board_balance_supported(void) { return 1u; }
uint8_t bms_board_heater_allowed(void) { return 1u; }
#else
void bms_board_features_init(void)
{
    /* D014 has no verified heater/fuse output path. Never touch the legacy
     * PB4/PB5 D011 pins when heater support is disabled. */
    if (SH3673510_PRODUCT_HEATER_SUPPORTED)
    {
        sh3673510_board_force_heater_fuse_safe();
        sh3673510_board_set_heater(0u);
    }
}

uint8_t bms_board_charge_source_present(void)
{
    /* D014 has no separate schematic-backed charger-present GPIO. Charger
     * detection for generic policy must come from a validated AFE/current
     * semantic source. Fail safe here; heater is disabled on D014 anyway. */
    return 0u;
}

uint8_t bms_board_heater_supported(void)
{
    return SH3673510_PRODUCT_HEATER_SUPPORTED ? 1u : 0u;
}

uint8_t bms_board_balance_supported(void)
{
    return SH3673510_PRODUCT_BALANCE_SUPPORTED ? 1u : 0u;
}

uint8_t bms_board_heater_allowed(void)
{
    /* Physical heater capability is the only board-level allow gate. */
    return SH3673510_PRODUCT_HEATER_SUPPORTED ? 1u : 0u;
}

void bms_board_heater_set(uint8_t enabled)
{
    if (SH3673510_PRODUCT_HEATER_SUPPORTED)
        sh3673510_board_set_heater(enabled ? 1u : 0u);
    else
        (void)enabled;
}

uint8_t bms_board_heater_fuse_supported(void) { return 0u; }
uint16_t bms_board_heater_off_fault_temp_x10(void) { return 0u; }
uint16_t bms_board_heater_off_fault_confirm_ms(void) { return 0u; }
void bms_board_heater_fuse_fire(void) { /* No validated automatic SH fuse policy. */ }
#endif
