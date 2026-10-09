/*
 * 文件功能：SH3673510 寄存器控制与硬件保护量化；执行配置验证、MOS/均衡、Sleep/Wake
 * 等器件流程。
 * bms/afe/sh3673510/sh3673510_control.h；实际编译归属见各产品 sources.txt。
 */
#ifndef SH3673510_CONTROL_H_
#define SH3673510_CONTROL_H_

#include <stdint.h>

typedef struct
{
    uint8_t flag1;
    uint8_t flag2;
    uint8_t bstatus1;
    uint8_t bstatus2;
} sh3673510_control_status_t;

typedef struct
{
    uint16_t ov_mv;
    uint16_t uv_mv;
    uint16_t ocd1_a10;
    uint16_t ocd2_a10;
    uint16_t occ_a10;
    uint16_t ov_delay_ms;
    uint16_t uv_delay_ms;
    uint16_t ocd1_delay_ms;
    uint16_t ocd2_delay_ms;
    uint16_t occ_delay_ms;
    uint16_t sc_a10;
    uint16_t sc_delay_us;
    uint8_t valid;
} sh3673510_protection_actual_t;

/* 初始化 SH 控制层并验证固定配置。 */
uint8_t sh3673510_control_init(void);
/* 将独立硬件保护配置量化、写入并验证寄存器。 */
uint8_t sh3673510_control_apply_protection(void);
/* 取得 SH 硬件实际可表示的保护配置。 */
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);
/* 根据请求设置充放电 FET 控制位。 */
uint8_t sh3673510_control_set_fets(uint8_t charge_on, uint8_t discharge_on);
/* 读取 SH 状态、保护标志及控制寄存器。 */
uint8_t sh3673510_control_read_status(sh3673510_control_status_t *status);
/* CRLD_EN 共用，负载检测与 C+ ADC 不能同时运行。 */
uint8_t sh3673510_control_set_load_detection(uint8_t enabled, uint8_t *changed);
/* 清除指定 FLAG1 保护标志并检查结果。 */
uint8_t sh3673510_control_clear_flag1(uint8_t clear_mask);
/* 清除指定 FLAG2 保护标志并检查结果。 */
uint8_t sh3673510_control_clear_flag2(uint8_t clear_mask);
/* 写入有效电芯通道的均衡掩码。 */
uint8_t sh3673510_control_set_balance(uint32_t cell_mask);
/* 1 表示准备写入及 SLEEP 命令全部成功；这是传输证据，不是 AFE 休眠电流实测。 */
uint8_t sh3673510_control_sleep(void);
/* 执行 SH 唤醒并重建配置、验证状态。 */
uint8_t sh3673510_control_wake(void);
/* 查询 SH 控制层的就绪状态。 */
uint8_t sh3673510_control_ready(void);
/* 仅在产品支持时驱动板级加热输出。 */
void sh3673510_board_set_heater(uint8_t enabled);
/* 将支持的加热/熔断引脚保持安全电平。 */
void sh3673510_board_force_heater_fuse_safe(void);
/* 查询板级 AFE 唤醒信号状态。 */
uint8_t sh3673510_board_wake_active(void);

#endif /* 头文件保护：SH3673510_CONTROL_H_。 */
