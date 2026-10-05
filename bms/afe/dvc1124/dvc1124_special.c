/*
 * 文件功能：DVC1124 特殊温度、告警清除和电流 ADC 校准操作；保留器件专属流程。
 * bms/afe/dvc1124/dvc1124_special.c；实际编译归属见各产品 sources.txt。
 */
#include "dvc1124.h"

/*
 * DVC1124 特殊寄存器处理。W0C、读清除、自清除语义与通用寄存器/配置路径分离，
 * 明确访问行为，避免普通读改写辅助函数误清硬件事件。
 */

/*
 * DVC1124-2 0x76 混合 COTF（位 7，读清除）和 COTT[6:0]（RW）。
 * 阈值读取必然消耗硬件标志，软件必须保留每次观察到的 COTF。
 */
static uint8_t s_core_ot_event_latched;

/* 读取核心过温寄存器原始值。 */
static uint8_t dvc1124_read_core_ot_raw(uint8_t *raw)
{
    if (raw == 0) return 0u;
    if (!DVC1124_ReadRegisters(DVC1124_REG_CORE_OT, raw, 1u)) return 0u;

    if ((*raw & DVC1124_CORE_OT_FLAG_MASK) != 0u)
        s_core_ot_event_latched = 1u;

    return 1u;
}

/* 读取核心过温阈值编码。 */
uint8_t DVC1124_ReadCoreOtThresholdCode(uint8_t *threshold_code)
{
    uint8_t raw;

    if (threshold_code == 0) return 0u;
    if (!dvc1124_read_core_ot_raw(&raw)) return 0u;

    *threshold_code = DVC1124_FIELD_GET(DVC1124_CORE_OT_THRESHOLD_MASK,
                                        DVC1124_CORE_OT_THRESHOLD_SHIFT,
                                        raw);
    return 1u;
}

/* 校验并设置核心过温阈值编码。 */
uint8_t DVC1124_SetCoreOtThresholdCode(uint8_t threshold_code)
{
    uint8_t raw;
    uint8_t target;
    uint8_t verify;

    if (threshold_code > 0x7Fu) return 0u;

    /* 首次读取显式捕获并保留待处理 RC 事件。 */
    if (!dvc1124_read_core_ot_raw(&raw)) return 0u;
    (void)raw;

    /* COTF 是 RC 状态，不是配置；只写 COTT[6:0]。 */
    target = DVC1124_FIELD_PREP(DVC1124_CORE_OT_THRESHOLD_MASK,
                                DVC1124_CORE_OT_THRESHOLD_SHIFT,
                                threshold_code);
    if (!DVC1124_WriteRegisters(DVC1124_REG_CORE_OT, &target, 1u)) return 0u;

    /* 回读也可能观察到新 COTF，应先保留事件再校验。 */
    if (!dvc1124_read_core_ot_raw(&verify)) return 0u;
    return ((verify & DVC1124_CORE_OT_THRESHOLD_MASK) == target) ? 1u : 0u;
}

/* 读取核心过温事件锁存状态。 */
uint8_t DVC1124_GetCoreOtEventLatched(void)
{
    return s_core_ot_event_latched;
}

/* 清除核心过温事件锁存。 */
void DVC1124_ClearCoreOtEventLatched(void)
{
    s_core_ot_event_latched = 0u;
}

/* 按专属寄存器语义清除 DVC 告警位。 */
uint8_t DVC1124_ClearAlarmFlags(uint8_t flag_mask)
{
    uint8_t write_value;

    if (flag_mask == 0u) return 1u;

    /* V1.2：ALARM 为 W0C；0 清除选定位，1 保留其它位。 */
    write_value = (uint8_t)~flag_mask;
    return DVC1124_WriteRegisters(DVC1124_REG_ALARM, &write_value, 1u);
}

/* 发起 DVC 电流 ADC 校准。 */
uint8_t DVC1124_StartCadcCalibration(void)
{
    uint8_t value;

    /*
     * CAMZ 是自清除命令；只读取安全 CADC 控制字节，保留持久字段后置位 CAMZ，
     * 不要求回读为 1。
     */
    if (!DVC1124_ReadRegisters(DVC1124_REG_CADC_CTRL, &value, 1u)) return 0u;
    value |= DVC1124_CADC_CAMZ_MASK;
    return DVC1124_WriteRegisters(DVC1124_REG_CADC_CTRL, &value, 1u);
}
