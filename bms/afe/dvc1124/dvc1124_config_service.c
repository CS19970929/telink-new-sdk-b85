/*
 * 文件功能：DVC1124 参数的 requested/effective 与原始寄存器访问；
 * 校验写入和固定配置边界。
 * bms/afe/dvc1124/dvc1124_config_service.c；实际编译归属见各产品 sources.txt。
 */
#include "dvc1124_config_service.h"

#include "dvc1124.h"
#include "bms_afe.h"
#include "bms_afe_hw_profile.h"

/* 把 VADC 时间编码转换为微秒。 */
static u16 dvc_cfg_vadc_time_us(dvc1124_vadc_time_t code)
{
    static const u16 table[4] = {790u, 1540u, 3030u, 6020u};
    return table[(u8)code & 0x03u];
}

/* 把定时唤醒编码转换为秒。 */
static u16 dvc_cfg_timed_wake_seconds(dvc1124_timed_wake_t code)
{
    static const u16 table[16] = {
        0u, 10u, 20u, 30u, 40u, 50u, 60u, 120u,
        180u, 240u, 300u, 360u, 420u, 480u, 540u, 600u
    };
    return table[(u8)code & 0x0Fu];
}

#if DVC1124_HW_PROTECT_ENABLE
/* 把核心过温编码换算为温度报告值。 */
static u16 dvc_cfg_core_ot_x10_from_code(u8 code)
{
    s32 value;
    if (code == 0u) return 0u;
    value = ((s32)(1466u + (u16)code * 2u) * 24467) / 10000 - 2710;
    if (value < 0) value = 0;
    if (value > 0xFFFF) value = 0xFFFF;
    return (u16)value;
}
#endif

/* 将采样压差换算为 0.1 A 电流值。 */
static u16 dvc_cfg_current_x10_from_sense_uv(u32 sense_uv)
{
    dvc1124_config_t cfg;
    u32 value;

    DVC1124_GetConfig(&cfg);
    if (cfg.shunt_uohm == 0u) return 0u;
    value = (sense_uv * 10u + cfg.shunt_uohm / 2u) / cfg.shunt_uohm;
    if (value > 0xFFFFu) value = 0xFFFFu;
    return (u16)value;
}

/* RM V1.2 p18-20、23-24：单字段与整组回读共用物理量解码。 */
static u16 dvc_cfg_protection_value(u8 reg, const u8 *data)
{
    static const u16 voltage_delay[16] = {
        200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
        1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
    };
    u16 code;
    switch (reg) {
    case DVC1124_REG_COV_H:
    case DVC1124_REG_CUV_H:
        code = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        return reg == DVC1124_REG_COV_H && code ? (u16)(code + 500u) : code;
    case DVC1124_REG_COV_L_DLY:
    case DVC1124_REG_CUV_L_DLY:
        return voltage_delay[data[0] & 0x0Fu];
    case DVC1124_REG_OCD1_THR:
    case DVC1124_REG_OCC1_THR:
        return dvc_cfg_current_x10_from_sense_uv((u32)data[0] * 250u);
    case DVC1124_REG_OCD1_DLY:
    case DVC1124_REG_OCC1_DLY:
        return (u16)((data[0] + 1u) * 8u);
    case DVC1124_REG_OCD2:
    case DVC1124_REG_OCC2:
        return (data[0] & DVC1124_OC2_ENABLE_MASK) ?
            dvc_cfg_current_x10_from_sense_uv(
                (u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u) : 0u;
    case DVC1124_REG_OCD2_DLY:
    case DVC1124_REG_OCC2_DLY:
        return (u16)((data[0] + 1u) * 4u);
    case DVC1124_REG_SCD:
        return (data[0] & DVC1124_SCD_ENABLE_MASK) ?
            (u16)(data[0] & DVC1124_SCD_THRESHOLD_MASK) * 10u : 0u;
    case DVC1124_REG_SCD_DLY:
        return (u16)(((u32)data[0] * 781u + 50u) / 100u);
    default:
        return 0u;
    }
}

/* 旧语义窗口仍只读取该字段涉及的寄存器，不扩大 I/O 范围。 */
static dvc1124_config_result_t dvc_cfg_read_effective(
    dvc1124_config_field_t field, u32 *value)
{
    static const u8 registers[] = {
        DVC1124_REG_COV_H, DVC1124_REG_COV_L_DLY,
        DVC1124_REG_CUV_H, DVC1124_REG_CUV_L_DLY,
        DVC1124_REG_OCD1_THR, DVC1124_REG_OCD1_DLY,
        DVC1124_REG_OCC1_THR, DVC1124_REG_OCC1_DLY,
        DVC1124_REG_OCD2, DVC1124_REG_OCD2_DLY,
        DVC1124_REG_OCC2, DVC1124_REG_OCC2_DLY,
        DVC1124_REG_SCD, DVC1124_REG_SCD_DLY
    };
    u8 data[2], reg, start, length;
    u16 offset = (u16)field - DVC1124_CFG_EFF_COV_MV;
    if (!value) return DVC1124_CFG_ERR_VALUE;
    if (offset >= sizeof(registers)) return DVC1124_CFG_ERR_ADDRESS;
    reg = registers[offset];
    start = reg;
    length = 1u;
    if (reg == DVC1124_REG_COV_L_DLY || reg == DVC1124_REG_CUV_L_DLY) --start;
    if (start == DVC1124_REG_COV_H || start == DVC1124_REG_CUV_H) length = 2u;
    if (!DVC1124_ReadRegisters(start, data, length))
        return DVC1124_CFG_ERR_AFE_IO;
    *value = dvc_cfg_protection_value(reg, data + (reg - start));
    return DVC1124_CFG_OK;
}

/* DS V1.1 p23：配置寄存器支持连续读；三个块均不包含 RC 状态。 */
u8 DVC1124_ReadProtectionEffective(bms_afe_hw_profile_t *p)
{
    u8 voltage[4], oc1[4], oc2[6];
    u8 sc;
    if (!p || !bms_afe_bus_access_allowed()) return 0u;
    sc = (p->enable_mask & BMS_AFE_HW_EN_SC) != 0u;
    if (!DVC1124_ReadRegisters(DVC1124_REG_COV_H, voltage, sizeof(voltage)) ||
        !DVC1124_ReadRegisters(DVC1124_REG_OCD1_THR, oc1, sizeof(oc1)) ||
        !DVC1124_ReadRegisters(DVC1124_REG_OCD2, oc2, sc ? 6u : 4u) ||
        (sc && DVC1124_DEFAULT_SHUNT_UOHM == 0u)) return 0u;
    /* 全部块通过底层 CRC/重试后才更新输出，失败不发布部分结果。 */
    p->cov_mv = dvc_cfg_protection_value(DVC1124_REG_COV_H, voltage);
    p->cov_delay_ms = dvc_cfg_protection_value(DVC1124_REG_COV_L_DLY, voltage + 1u);
    p->cuv_mv = dvc_cfg_protection_value(DVC1124_REG_CUV_H, voltage + 2u);
    p->cuv_delay_ms = dvc_cfg_protection_value(DVC1124_REG_CUV_L_DLY, voltage + 3u);
    p->ocd1_a10 = dvc_cfg_protection_value(DVC1124_REG_OCD1_THR, oc1);
    p->occ1_a10 = dvc_cfg_protection_value(DVC1124_REG_OCC1_THR, oc1 + 1u);
    p->ocd1_delay_ms = dvc_cfg_protection_value(DVC1124_REG_OCD1_DLY, oc1 + 2u);
    p->occ1_delay_ms = dvc_cfg_protection_value(DVC1124_REG_OCC1_DLY, oc1 + 3u);
    p->ocd2_a10 = dvc_cfg_protection_value(DVC1124_REG_OCD2, oc2);
    p->occ2_a10 = dvc_cfg_protection_value(DVC1124_REG_OCC2, oc2 + 1u);
    p->ocd2_delay_ms = dvc_cfg_protection_value(DVC1124_REG_OCD2_DLY, oc2 + 2u);
    p->occ2_delay_ms = dvc_cfg_protection_value(DVC1124_REG_OCC2_DLY, oc2 + 3u);
    if (sc) {
        p->sc_a10 = (u16)(((u32)dvc_cfg_protection_value(DVC1124_REG_SCD, oc2 + 4u) *
                           10000u) / DVC1124_DEFAULT_SHUNT_UOHM);
        p->sc_delay_us = dvc_cfg_protection_value(DVC1124_REG_SCD_DLY, oc2 + 5u);
    }
    return 1u;
}

/* 读取请求的独立硬件保护参数。 */
static dvc1124_config_result_t dvc_cfg_read_requested_protection(
    dvc1124_config_field_t field,
    u32 *value)
{
    bms_afe_hw_profile_t hw;
    dvc1124_config_t device;
    u32 sense_uv;

    if (value == 0) return DVC1124_CFG_ERR_VALUE;
    if (!bms_afe_hw_profile_get(&hw)) return DVC1124_CFG_ERR_STORE;
    DVC1124_GetConfig(&device);

    switch (field)
    {
    case DVC1124_CFG_REQ_COV_MV:        *value = hw.cov_mv; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_COV_DELAY_MS:  *value = hw.cov_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_CUV_MV:        *value = hw.cuv_mv; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_CUV_DELAY_MS:  *value = hw.cuv_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD1_X10A:     *value = hw.ocd1_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD1_DELAY_MS: *value = hw.ocd1_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC1_X10A:     *value = hw.occ1_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC1_DELAY_MS: *value = hw.occ1_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD2_X10A:     *value = hw.ocd2_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD2_DELAY_MS: *value = hw.ocd2_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC2_X10A:     *value = hw.occ2_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC2_DELAY_MS: *value = hw.occ2_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_SCD_MV:
        if (device.shunt_uohm == 0u) return DVC1124_CFG_ERR_VALUE;
        sense_uv = ((u32)hw.sc_a10 * device.shunt_uohm) / 10u;
        *value = sense_uv / 1000u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_SCD_DELAY_US:
        *value = hw.sc_delay_us;
        return DVC1124_CFG_OK;
    default:
        return DVC1124_CFG_ERR_ADDRESS;
    }
}

/* 判断配置字段是否由固件固定策略拥有。 */
static uint8_t dvc_cfg_fixed_field(dvc1124_config_field_t field)
{
    u8 f = (u8)field;
    if (f <= (u8)DVC1124_CFG_CONFIG_INCONSISTENT) return 1u;
    if (f >= (u8)DVC1124_CFG_HS_FET_MASK &&
        f <= (u8)DVC1124_CFG_VADC_TIME_US) return 1u;
    if (f >= (u8)DVC1124_CFG_GP1_MODE &&
        f <= (u8)DVC1124_CFG_GP6_MODE) return 1u;
    if (f >= (u8)DVC1124_CFG_V3P3_SLEEP_ENABLE &&
        f <= (u8)DVC1124_CFG_INTERRUPT_MASK) return 1u;
    if (f >= (u8)DVC1124_CFG_CURRENT_WAKE_UV &&
        f <= (u8)DVC1124_CFG_CORE_OT_X10C) return 1u;
    return 0u;
}

/* 读取 DVC 语义配置窗口中的指定字段。 */
dvc1124_config_result_t DVC1124_ConfigServiceRead(dvc1124_config_field_t field,
                                                   u32 *value)
{
    dvc1124_config_t device;
    dvc1124_snapshot_t snapshot;
    u8 chip;

    if (value == 0) return DVC1124_CFG_ERR_VALUE;
    if (!bms_afe_bus_access_allowed()) return DVC1124_CFG_ERR_AFE_IO;

    if ((u8)field >= (u8)DVC1124_CFG_EFF_COV_MV &&
        (u8)field <= (u8)DVC1124_CFG_EFF_SCD_DELAY_US)
        return dvc_cfg_read_effective(field, value);

    if ((u8)field >= (u8)DVC1124_CFG_REQ_COV_MV &&
        (u8)field <= (u8)DVC1124_CFG_REQ_SCD_DELAY_US)
        return dvc_cfg_read_requested_protection(field, value);

    DVC1124_GetConfig(&device);
    switch (field)
    {
    case DVC1124_CFG_SCHEMA:         *value = DVC1124_CONFIG_SCHEMA_VERSION; return DVC1124_CFG_OK;
    case DVC1124_CFG_MODEL:          *value = device.model; return DVC1124_CFG_OK;
    case DVC1124_CFG_CHIP_VERSION:
        if (!DVC1124_ReadRegisters(DVC1124_REG_CHIP_VERSION, &chip, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = chip;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_WRITE_ADDR:     *value = DVC1124_GetWriteAddress(); return DVC1124_CFG_OK;
    case DVC1124_CFG_CELL_COUNT:     *value = device.cell_count; return DVC1124_CFG_OK;
    case DVC1124_CFG_SHUNT_UOHM_LO:  *value = device.shunt_uohm & 0xFFFFu; return DVC1124_CFG_OK;
    case DVC1124_CFG_SHUNT_UOHM_HI:  *value = device.shunt_uohm >> 16; return DVC1124_CFG_OK;
    case DVC1124_CFG_STATUS_CACHED:
        DVC1124_GetSnapshot(&snapshot);
        *value = snapshot.status;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_CORE_OT_EVENT_LATCHED:
        *value = DVC1124_GetCoreOtEventLatched();
        return DVC1124_CFG_OK;
    case DVC1124_CFG_CONFIG_INCONSISTENT:
        *value = 0u;
        return DVC1124_CFG_OK;

    case DVC1124_CFG_HS_FET_MASK:           *value = DVC1124_DEFAULT_HIGH_SIDE_FET_MASK; return DVC1124_CFG_OK;
    case DVC1124_CFG_CADC_WORK_ENABLE:       *value = DVC1124_DEFAULT_CADC_WORK_ENABLE; return DVC1124_CFG_OK;
    case DVC1124_CFG_CURRENT_WAKE_ENABLE:
#if DVC1124_HW_PROTECT_ENABLE
        *value = DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE;
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    case DVC1124_CFG_CC1_WORK_TIME:          *value = DVC1124_DEFAULT_CC1_WORK_TIME; return DVC1124_CFG_OK;
    case DVC1124_CFG_CC1_SLEEP_WAKE_TIME:    *value = DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME; return DVC1124_CFG_OK;
    case DVC1124_CFG_CHARGE_PUMP_VOLTAGE:
        *value = DVC1124_CHARGE_PUMP_VOLTAGE_CODE
                     ? (u32)DVC1124_CHARGE_PUMP_VOLTAGE_CODE + 5u
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_CELL_MEAS_MASK:         *value = DVC1124_DEFAULT_CELL_MEASUREMENT_MASK; return DVC1124_CFG_OK;
    case DVC1124_CFG_CELL_SIGNED_MODE:       *value = DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED; return DVC1124_CFG_OK;
    case DVC1124_CFG_VADC_ENABLE:            *value = DVC1124_DEFAULT_VADC_ENABLE; return DVC1124_CFG_OK;
    case DVC1124_CFG_VADC_SYNC:              *value = DVC1124_DEFAULT_VADC_SYNC_WITH_CC2; return DVC1124_CFG_OK;
    case DVC1124_CFG_VADC_PERIOD_CYCLES:     *value = 1u << (u8)DVC1124_DEFAULT_VADC_PERIOD; return DVC1124_CFG_OK;
    case DVC1124_CFG_VADC_TIME_US:           *value = dvc_cfg_vadc_time_us(DVC1124_DEFAULT_VADC_TIME); return DVC1124_CFG_OK;
    case DVC1124_CFG_GP1_MODE:               *value = DVC1124_GP1_DEFAULT_MODE; return DVC1124_CFG_OK;
    case DVC1124_CFG_GP2_MODE:               *value = DVC1124_GP2_DEFAULT_MODE; return DVC1124_CFG_OK;
    case DVC1124_CFG_GP3_MODE:               *value = DVC1124_GP3_DEFAULT_MODE; return DVC1124_CFG_OK;
    case DVC1124_CFG_GP4_MODE:               *value = DVC1124_GP4_DEFAULT_MODE; return DVC1124_CFG_OK;
    case DVC1124_CFG_GP5_MODE:               *value = DVC1124_GP5_DEFAULT_MODE; return DVC1124_CFG_OK;
    case DVC1124_CFG_GP6_MODE:               *value = DVC1124_GP6_DEFAULT_MODE; return DVC1124_CFG_OK;
    case DVC1124_CFG_V3P3_SLEEP_ENABLE:      *value = DVC1124_DEFAULT_V3P3_SLEEP_ENABLE; return DVC1124_CFG_OK;
    case DVC1124_CFG_V3P3_WORK_ENABLE:       *value = DVC1124_DEFAULT_V3P3_WORK_ENABLE; return DVC1124_CFG_OK;
    case DVC1124_CFG_V3P3_TIMEOUT_RESTART:   *value = DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART; return DVC1124_CFG_OK;
    case DVC1124_CFG_I2C_WDT_SECONDS:
#if DVC1124_HW_PROTECT_ENABLE
        *value = DVC1124_I2C_WATCHDOG_SECONDS;
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    case DVC1124_CFG_TIMED_WAKE_SECONDS:     *value = dvc_cfg_timed_wake_seconds(DVC1124_DEFAULT_TIMED_WAKE); return DVC1124_CFG_OK;
    case DVC1124_CFG_INTERRUPT_MASK:         *value = DVC1124_DEFAULT_INTERRUPT_MASK; return DVC1124_CFG_OK;
    case DVC1124_CFG_CURRENT_WAKE_UV:
#if DVC1124_HW_PROTECT_ENABLE
        *value = DVC1124_CURRENT_WAKE_THRESHOLD_UV;
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    case DVC1124_CFG_BODY_DIODE_UV:
#if DVC1124_HW_PROTECT_ENABLE
        *value = DVC1124_BODY_DIODE_THRESHOLD_UV;
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    case DVC1124_CFG_DSG_PULLDOWN:           *value = DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH; return DVC1124_CFG_OK;
    case DVC1124_CFG_I2C_TIMEOUT_CLOSE_CHG:
#if DVC1124_HW_PROTECT_ENABLE
        *value = DVC1124_I2C_TIMEOUT_CLOSE_CHG ? 1u : 0u;
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    case DVC1124_CFG_I2C_TIMEOUT_CLOSE_DSG:
#if DVC1124_HW_PROTECT_ENABLE
        *value = DVC1124_I2C_TIMEOUT_CLOSE_DSG ? 1u : 0u;
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    case DVC1124_CFG_CORE_OT_X10C:
#if DVC1124_HW_PROTECT_ENABLE
        *value = dvc_cfg_core_ot_x10_from_code(DVC1124_DEFAULT_CORE_OT_CODE);
#else
        *value = 0u;
#endif
        return DVC1124_CFG_OK;
    default:
        return DVC1124_CFG_ERR_ADDRESS;
    }
}

/* 拒绝固定配置和保护窗口写入，返回只读或地址错误。 */
dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t field,
                                                    u32 value)
{
    (void)value;

    /* 固定 DVC 运行/板级策略归固件拥有；保护写入走专用软件保护与 AFE 硬件配置事务。 */
    if (dvc_cfg_fixed_field(field) ||
        ((u8)field >= (u8)DVC1124_CFG_REQ_COV_MV &&
         (u8)field <= (u8)DVC1124_CFG_REQ_SCD_DELAY_US) ||
        ((u8)field >= (u8)DVC1124_CFG_EFF_COV_MV &&
         (u8)field <= (u8)DVC1124_CFG_EFF_SCD_DELAY_US))
        return DVC1124_CFG_ERR_READ_ONLY;

    return DVC1124_CFG_ERR_ADDRESS;
}

#if BMS_DVC_RAW_DIAG_ENABLE
/* 按允许范围读取 DVC 原始寄存器。 */
dvc1124_config_result_t DVC1124_ConfigServiceReadRaw(u8 reg, u8 *value)
{
    if (value == 0 || reg > DVC1124_MAX_REGISTER)
        return DVC1124_CFG_ERR_ADDRESS;
    if (!bms_afe_bus_access_allowed()) return DVC1124_CFG_ERR_AFE_IO;

    /* STATUS 和 CORE_OT 含读清除字段。 */
    if (DVC1124_RegReadHasSideEffect(reg))
        return DVC1124_CFG_ERR_FORBIDDEN;

    return DVC1124_ReadRegisters(reg, value, 1u)
               ? DVC1124_CFG_OK
               : DVC1124_CFG_ERR_AFE_IO;
}

/* 拒绝原始寄存器写入，返回只读或地址错误。 */
dvc1124_config_result_t DVC1124_ConfigServiceWriteRaw(u8 reg, u8 value)
{
    (void)value;
    if (reg > DVC1124_MAX_REGISTER) return DVC1124_CFG_ERR_ADDRESS;

    /*
     * 原始镜像仅供诊断。允许原始写入会产生第二个编译期产品配置所有者，
     * 或绕过保护参数管理。
     */
    return DVC1124_CFG_ERR_READ_ONLY;
}

#endif
