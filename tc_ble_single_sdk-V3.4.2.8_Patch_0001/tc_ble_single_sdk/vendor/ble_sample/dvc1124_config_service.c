#include "dvc1124_config_service.h"

#include "dvc1124.h"
#include "bms_afe.h"
#include "bms_afe_hw_profile.h"

static u16 dvc_cfg_vadc_time_us(dvc1124_vadc_time_t code)
{
    static const u16 table[4] = {790u, 1540u, 3030u, 6020u};
    return table[(u8)code & 0x03u];
}

static u16 dvc_cfg_timed_wake_seconds(dvc1124_timed_wake_t code)
{
    static const u16 table[16] = {
        0u, 10u, 20u, 30u, 40u, 50u, 60u, 120u,
        180u, 240u, 300u, 360u, 420u, 480u, 540u, 600u
    };
    return table[(u8)code & 0x0Fu];
}

static u16 dvc_cfg_core_ot_x10_from_code(u8 code)
{
    s32 value;
    if (code == 0u) return 0u;
    value = ((s32)(1466u + (u16)code * 2u) * 24467) / 10000 - 2710;
    if (value < 0) value = 0;
    if (value > 0xFFFF) value = 0xFFFF;
    return (u16)value;
}

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

static dvc1124_config_result_t dvc_cfg_read_effective(
    dvc1124_config_field_t field,
    u32 *value)
{
    u8 data[2];
    u16 code12;

    if (value == 0) return DVC1124_CFG_ERR_VALUE;

    switch (field)
    {
    case DVC1124_CFG_EFF_COV_MV:
    case DVC1124_CFG_EFF_COV_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_COV_H, data, 2u))
            return DVC1124_CFG_ERR_AFE_IO;
        code12 = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        if (field == DVC1124_CFG_EFF_COV_MV)
            *value = code12 ? (u32)code12 + 500u : 0u;
        else
        {
            static const u16 delay[16] = {
                200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
                1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
            };
            *value = delay[data[1] & 0x0Fu];
        }
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_CUV_MV:
    case DVC1124_CFG_EFF_CUV_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_CUV_H, data, 2u))
            return DVC1124_CFG_ERR_AFE_IO;
        code12 = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        if (field == DVC1124_CFG_EFF_CUV_MV)
            *value = code12;
        else
        {
            static const u16 delay[16] = {
                200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
                1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
            };
            *value = delay[data[1] & 0x0Fu];
        }
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_OCD1_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD1_THR, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = data[0]
                     ? dvc_cfg_current_x10_from_sense_uv((u32)data[0] * 250u)
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCD1_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD1_DLY, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 8u);
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC1_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC1_THR, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = data[0]
                     ? dvc_cfg_current_x10_from_sense_uv((u32)data[0] * 250u)
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC1_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC1_DLY, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 8u);
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_OCD2_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD2, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (data[0] & DVC1124_OC2_ENABLE_MASK)
                     ? dvc_cfg_current_x10_from_sense_uv(
                           (u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u)
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCD2_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD2_DLY, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 4u);
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC2_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC2, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (data[0] & DVC1124_OC2_ENABLE_MASK)
                     ? dvc_cfg_current_x10_from_sense_uv(
                           (u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u)
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC2_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC2_DLY, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 4u);
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_SCD_MV:
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = (data[0] & DVC1124_SCD_ENABLE_MASK)
                     ? (u32)(data[0] & DVC1124_SCD_THRESHOLD_MASK) * 10u
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_SCD_DELAY_US:
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD_DLY, data, 1u))
            return DVC1124_CFG_ERR_AFE_IO;
        *value = ((u32)data[0] * 781u + 50u) / 100u;
        return DVC1124_CFG_OK;
    default:
        return DVC1124_CFG_ERR_ADDRESS;
    }
}

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

dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t field,
                                                    u32 value)
{
    (void)value;

    /* Fixed DVC operating/board policy is firmware-owned.  Protection writes
     * use the dedicated software-protection and AFE HW-profile transactions. */
    if (dvc_cfg_fixed_field(field) ||
        ((u8)field >= (u8)DVC1124_CFG_REQ_COV_MV &&
         (u8)field <= (u8)DVC1124_CFG_REQ_SCD_DELAY_US) ||
        ((u8)field >= (u8)DVC1124_CFG_EFF_COV_MV &&
         (u8)field <= (u8)DVC1124_CFG_EFF_SCD_DELAY_US))
        return DVC1124_CFG_ERR_READ_ONLY;

    return DVC1124_CFG_ERR_ADDRESS;
}

dvc1124_config_result_t DVC1124_ConfigServiceReadRaw(u8 reg, u8 *value)
{
    if (value == 0 || reg > DVC1124_MAX_REGISTER)
        return DVC1124_CFG_ERR_ADDRESS;
    if (!bms_afe_bus_access_allowed()) return DVC1124_CFG_ERR_AFE_IO;

    /* STATUS and CORE_OT contain read-clear fields. */
    if (DVC1124_RegReadHasSideEffect(reg))
        return DVC1124_CFG_ERR_FORBIDDEN;

    return DVC1124_ReadRegisters(reg, value, 1u)
               ? DVC1124_CFG_OK
               : DVC1124_CFG_ERR_AFE_IO;
}

dvc1124_config_result_t DVC1124_ConfigServiceWriteRaw(u8 reg, u8 value)
{
    (void)value;
    if (reg > DVC1124_MAX_REGISTER) return DVC1124_CFG_ERR_ADDRESS;

    /* The raw mirror is diagnostic-only.  Allowing a raw write would create a
     * second owner for compile-time product configuration or bypass protection
     * parameter bookkeeping. */
    return DVC1124_CFG_ERR_READ_ONLY;
}
