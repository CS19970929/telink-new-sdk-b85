/*
 * 文件功能：软件三级保护滤波与故障位更新；使用 g_bms_protection_params，
 * Third 负责 MOS 阻断与恢复回差。
 * bms/core/bms_sw_protection.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_SW_PROTECTION_H_
#define BMS_SW_PROTECTION_H_

#include <stdint.h>
#include "bms_protection_params.h"

/*
 * 与 AFE 无关的软件保护输入。温度为 (degC+40)*10；电压/电流取 g_bms_report，
 * 旧单位见 bms_state.h。
 */
typedef struct
{
    uint8_t battery_temp_valid;
    uint8_t mos_temp_valid;
    uint16_t battery_temp_min;
    uint16_t battery_temp_max;
    uint16_t mos_temp;
    /*
     * 产品能力不由失败样本推断；当前产品均需电池 NTC。
     * 未安装的 MOS NTC 既不是断线故障，也不参与 MOS 温度保护。
     */
    uint8_t mos_temp_required;
    /*
     * 后端可选提供物理证据：Third 过流恢复除电流回差外还需新物理解除证据；
     * 零值兼容未提供该输入的后端。
     */
    uint8_t current_recovery_requires_evidence;
    uint8_t charge_recovery_allowed;
    uint8_t discharge_recovery_allowed;
    uint8_t current_recovery_sample_fresh;
} bms_sw_protection_inputs_t;

/* 检查软件保护阈值、恢复值和延时关系。 */
uint8_t bms_sw_protection_validate_params(const bms_protection_params_t *params);
/* 初始化软件保护参数和状态。 */
void bms_sw_protection_init(void);
/* 清除软件保护状态与内部滤波计数。 */
void bms_sw_protection_clear(void);
/* 作废部分物理恢复窗口，不清除活动故障。 */
void bms_sw_protection_reset_current_recovery(void);
/* 按当前测量快照更新软件保护状态。 */
void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs);
/* 独立策略分组，禁用分组清除自己拥有的状态。 */
void bms_sw_protection_update_groups(const bms_sw_protection_inputs_t *inputs,
                                     uint8_t voltage_current_enabled,
                                     uint8_t temperature_enabled);
/* 记录软件保护故障的触发边沿与历史编号。 */
void bms_sw_protection_record_fault_edges(void);
/* 查询三级软件保护是否禁止充电。 */
uint8_t bms_sw_protection_charge_blocked(void);
/* 查询三级软件保护是否禁止放电。 */
uint8_t bms_sw_protection_discharge_blocked(void);
/* 查询软件放电过流三级保护是否激活。 */
uint8_t bms_sw_protection_discharge_overcurrent_active(void);

#endif /* 头文件保护：BMS_SW_PROTECTION_H_。 */
