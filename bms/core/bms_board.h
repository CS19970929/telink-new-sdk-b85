/*
 * 文件功能：产品板级能力与 GPIO 操作；通过产品配置选择 heater/fuse/均衡路径，
 * 保持四产品硬件边界。
 * bms/core/bms_board.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_BOARD_H_
#define BMS_BOARD_H_

#include <stdint.h>

/*
 * 仅板级功能边界。公共加热/均衡/断线策略不应知道 MCU GPIO 编号，
 * 也不在此直接访问 AFE 寄存器；
 * 仅提供充电器存在、加热输出及不可逆保险丝触发等产品物理信号。
 */
void bms_board_features_init(void);
/* 查询产品是否具有有效电芯均衡通道。 */
uint8_t bms_board_balance_supported(void);
/* 查询板级硬件能力是否允许公共加热策略。 */
uint8_t bms_board_heater_allowed(void);
/* 按产品已验证硬件输入查询充电源存在状态。 */
uint8_t bms_board_charge_source_present(void);
/* 查询当前产品是否有已支持的物理加热输出。 */
uint8_t bms_board_heater_supported(void);
/* 仅在产品支持时设置物理加热输出。 */
void bms_board_heater_set(uint8_t enabled);
/* 查询是否有支持的不可逆加热熔断输出。 */
uint8_t bms_board_heater_fuse_supported(void);
/* 取得关闭加热后异常温度的报告编码阈值。 */
uint16_t bms_board_heater_off_fault_temp_x10(void);
/* 取得关闭加热后异常温度的确认毫秒数。 */
uint16_t bms_board_heater_off_fault_confirm_ms(void);
/* 仅在产品支持时触发不可逆加热熔断。 */
void bms_board_heater_fuse_fire(void);

#endif /* 头文件保护：BMS_BOARD_H_。 */
