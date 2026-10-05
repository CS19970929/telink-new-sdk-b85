/* 文件功能：软件三级保护滤波与故障位更新；使用 g_tParam.protect，Third 负责 MOS 阻断与恢复回差。
 * bms/core/bms_sw_protection.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_SW_PROTECTION_H_
#define BMS_SW_PROTECTION_H_

#include <stdint.h>
#include "bms_protection_params.h"

/*
 * AFE-independent software protection input.
 * Temperatures use the existing firmware encoding: (degC + 40) * 10.
 * Voltage/current values are read from g_stCellInfoReport in the legacy units
 * documented by bms_state.h.
 */
typedef struct
{
    uint8_t battery_temp_valid;
    uint8_t mos_temp_valid;
    uint16_t battery_temp_min;
    uint16_t battery_temp_max;
    uint16_t mos_temp;
    /* Product capability, never inferred from a failed sample. Battery NTCs
     * are required on every current product. An unfitted MOS NTC is neither
     * a sensor fault nor an input to MOS temperature protection. */
    uint8_t mos_temp_required;
} bms_sw_protection_inputs_t;

uint8_t bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *params);
void bms_sw_protection_init(void);
void bms_sw_protection_clear(void);
void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs);
/* Independent policy groups; disabled groups clear their owned state. */
void bms_sw_protection_update_groups(const bms_sw_protection_inputs_t *inputs,
                                     uint8_t voltage_current_enabled,
                                     uint8_t temperature_enabled);
void bms_sw_protection_record_fault_edges(void);
uint8_t bms_sw_protection_charge_blocked(void);
uint8_t bms_sw_protection_discharge_blocked(void);

#endif /* BMS_SW_PROTECTION_H_ */
