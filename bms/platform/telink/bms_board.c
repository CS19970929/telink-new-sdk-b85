/*
 * 文件功能：产品板级能力与 GPIO 操作；通过产品配置选择 heater/fuse/均衡路径，
 * 保持四产品硬件边界。
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
/* 初始化当前产品支持的板级功能 GPIO。 */
void bms_board_features_init(void)
{
    /* HS-D008：PA1 = MCC-EN-HT，PD4 = MCC-EN-RF；启动时均保持非激活。 */
    gpio_set_func(HEATER_EN_PIN, AS_GPIO);
    gpio_write(HEATER_EN_PIN, 0u);
    gpio_set_input_en(HEATER_EN_PIN, 0u);
    gpio_set_output_en(HEATER_EN_PIN, 1u);

    gpio_set_func(RF_EN_PIN, AS_GPIO);
    gpio_write(RF_EN_PIN, 0u);
    gpio_set_input_en(RF_EN_PIN, 0u);
    gpio_set_output_en(RF_EN_PIN, 1u);
}

/* 按产品已验证硬件输入查询充电源存在状态。 */
uint8_t bms_board_charge_source_present(void)
{
    /*
     * CHG-IN/PB1 用于负载检测，不能证明充电器存在。ACC 与负载策略尚未实现，
     * 不得因此允许加热。
     */
    return 0u;
}

/* 查询当前产品是否有已支持的物理加热输出。 */
uint8_t bms_board_heater_supported(void)
{
    return 1u;
}

/* 仅在产品支持时设置物理加热输出。 */
void bms_board_heater_set(uint8_t enabled)
{
    gpio_write(HEATER_EN_PIN, enabled ? 1u : 0u);
}

/* 查询是否有支持的不可逆加热熔断输出。 */
uint8_t bms_board_heater_fuse_supported(void)
{
    return 1u;
}

/* 取得关闭加热后异常温度的报告编码阈值。 */
uint16_t bms_board_heater_off_fault_temp_x10(void)
{
    return (uint16_t)DVC1124_HEATER_OFF_FAULT_TEMP_X10;
}

/* 取得关闭加热后异常温度的确认毫秒数。 */
uint16_t bms_board_heater_off_fault_confirm_ms(void)
{
    return (uint16_t)DVC1124_HEATER_OFF_FAULT_CONFIRM_MS;
}

/* 仅在产品支持时触发不可逆加热熔断。 */
void bms_board_heater_fuse_fire(void)
{
    /* HS-D008 不可逆加热电路故障处置：将 PD4 / MCC-EN-RF 置高。 */
    gpio_write(RF_EN_PIN, 1u);
}

/* 查询产品是否具有有效电芯均衡通道。 */
uint8_t bms_board_balance_supported(void) { return 1u; }
/* 查询板级硬件能力是否允许公共加热策略。 */
uint8_t bms_board_heater_allowed(void) { return 1u; }
#else
/* 初始化当前产品支持的板级功能 GPIO。 */
void bms_board_features_init(void)
{
    /* D014 没有经验证的加热/熔断输出；加热支持关闭时禁止操作遗留 D011 PB4/PB5。 */
    if (SH3673510_PRODUCT_HEATER_SUPPORTED)
    {
        sh3673510_board_force_heater_fuse_safe();
        sh3673510_board_set_heater(0u);
    }
}

/* 按产品已验证硬件输入查询充电源存在状态。 */
uint8_t bms_board_charge_source_present(void)
{
    /*
     * D014 无原理图支持的独立充电器存在 GPIO。
     * 公共策略必须从已验证的 AFE/电流语义判断；此处安全拒绝，D014 加热本就关闭。
     */
    return 0u;
}

/* 查询当前产品是否有已支持的物理加热输出。 */
uint8_t bms_board_heater_supported(void)
{
    return SH3673510_PRODUCT_HEATER_SUPPORTED ? 1u : 0u;
}

/* 查询产品是否具有有效电芯均衡通道。 */
uint8_t bms_board_balance_supported(void)
{
    return SH3673510_PRODUCT_BALANCE_SUPPORTED ? 1u : 0u;
}

/* 查询板级硬件能力是否允许公共加热策略。 */
uint8_t bms_board_heater_allowed(void)
{
    /* 板级加热允许条件仅由物理加热能力决定。 */
    return SH3673510_PRODUCT_HEATER_SUPPORTED ? 1u : 0u;
}

/* 仅在产品支持时设置物理加热输出。 */
void bms_board_heater_set(uint8_t enabled)
{
    if (SH3673510_PRODUCT_HEATER_SUPPORTED)
        sh3673510_board_set_heater(enabled ? 1u : 0u);
    else
        (void)enabled;
}

/* 查询是否有支持的不可逆加热熔断输出。 */
uint8_t bms_board_heater_fuse_supported(void) { return 0u; }
/* 取得关闭加热后异常温度的报告编码阈值。 */
uint16_t bms_board_heater_off_fault_temp_x10(void) { return 0u; }
/* 取得关闭加热后异常温度的确认毫秒数。 */
uint16_t bms_board_heater_off_fault_confirm_ms(void) { return 0u; }
/* 仅在产品支持时触发不可逆加热熔断。 */
void bms_board_heater_fuse_fire(void) { /* 尚无经验证的 SH 自动熔断策略。 */ }
#endif
