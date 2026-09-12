#include "dvc1124_config_service.h"

#include "dvc1124.h"
#include "dvc1124_config_store.h"
#include "bms_cold_kv_store.h"
#include "param.h"
#include "runtime.h"
#include <string.h>

static dvc1124_config_result_t dvc_cfg_load(dvc1124_persistent_config_t *cfg)
{
    if (cfg == NULL) return DVC1124_CFG_ERR_VALUE;
    if (DVC1124_ConfigStoreLoad(cfg)) return DVC1124_CFG_OK;

    DVC1124_ConfigStoreGetDefaults(cfg);
    return DVC1124_ConfigStoreValidate(cfg)
               ? DVC1124_CFG_OK
               : DVC1124_CFG_ERR_STORE;
}

static dvc1124_config_result_t dvc_cfg_apply_store_transaction(
    const dvc1124_persistent_config_t *before,
    const dvc1124_persistent_config_t *after)
{
    if ((before == NULL) || (after == NULL)) return DVC1124_CFG_ERR_VALUE;
    if (!DVC1124_ConfigStoreValidate(after)) return DVC1124_CFG_ERR_VALUE;

    if (!DVC1124_ConfigStoreApply(after)) return DVC1124_CFG_ERR_AFE_IO;

    if (!DVC1124_ConfigStoreSave(after))
    {
        /* Do not leave live AFE and persistent source-of-truth divergent. */
        (void)DVC1124_ConfigStoreApply(before);
        return DVC1124_CFG_ERR_STORE;
    }

    return DVC1124_CFG_OK;
}

static u16 dvc_cfg_vadc_time_us(dvc1124_vadc_time_t code)
{
    static const u16 table[4] = {790u, 1540u, 3030u, 6020u};
    return table[(u8)code & 0x03u];
}

static int dvc_cfg_vadc_time_code(u32 value, dvc1124_vadc_time_t *code)
{
    if (code == NULL) return 0;
    switch (value)
    {
    case 790u:  *code = DVC1124_VADC_TIME_0P79MS; return 1;
    case 1540u: *code = DVC1124_VADC_TIME_1P54MS; return 1;
    case 3030u: *code = DVC1124_VADC_TIME_3P03MS; return 1;
    case 6020u: *code = DVC1124_VADC_TIME_6P02MS; return 1;
    default: return 0;
    }
}

static u16 dvc_cfg_wdt_seconds(dvc1124_i2c_wdt_code_t code)
{
    switch (code)
    {
    case DVC1124_I2C_WDT_4S:  return 4u;
    case DVC1124_I2C_WDT_8S:  return 8u;
    case DVC1124_I2C_WDT_16S: return 16u;
    case DVC1124_I2C_WDT_32S: return 32u;
    default: return 0u;
    }
}

static int dvc_cfg_wdt_code(u32 seconds, dvc1124_i2c_wdt_code_t *code)
{
    if (code == NULL) return 0;
    switch (seconds)
    {
    case 0u:  *code = DVC1124_I2C_WDT_OFF; return 1;
    case 4u:  *code = DVC1124_I2C_WDT_4S; return 1;
    case 8u:  *code = DVC1124_I2C_WDT_8S; return 1;
    case 16u: *code = DVC1124_I2C_WDT_16S; return 1;
    case 32u: *code = DVC1124_I2C_WDT_32S; return 1;
    default: return 0;
    }
}

static u16 dvc_cfg_timed_wake_seconds(dvc1124_timed_wake_t code)
{
    static const u16 table[16] = {
        0u, 10u, 20u, 30u, 40u, 50u, 60u, 120u,
        180u, 240u, 300u, 360u, 420u, 480u, 540u, 600u
    };
    return table[(u8)code & 0x0Fu];
}

static int dvc_cfg_timed_wake_code(u32 seconds, dvc1124_timed_wake_t *code)
{
    static const u16 table[16] = {
        0u, 10u, 20u, 30u, 40u, 50u, 60u, 120u,
        180u, 240u, 300u, 360u, 420u, 480u, 540u, 600u
    };
    u8 i;

    if (code == NULL) return 0;
    for (i = 0u; i < 16u; ++i)
    {
        if (table[i] == seconds)
        {
            *code = (dvc1124_timed_wake_t)i;
            return 1;
        }
    }
    return 0;
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

static int dvc_cfg_core_ot_code(u32 requested_x10, u8 *code)
{
    u8 i;
    u8 best = 0u;

    if (code == NULL) return 0;
    if (requested_x10 == 0u)
    {
        *code = 0u;
        return 1;
    }

    for (i = 1u; i <= 127u; ++i)
    {
        u16 actual = dvc_cfg_core_ot_x10_from_code(i);
        if ((u32)actual <= requested_x10) best = i;
        else break;
    }

    if (best == 0u) return 0;
    *code = best;
    return 1;
}

static u16 dvc_cfg_voltage_delay_ms(u8 code)
{
    static const u16 table[16] = {
        200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
        1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
    };
    return table[code & 0x0Fu];
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

static dvc1124_config_result_t dvc_cfg_read_effective(dvc1124_config_field_t field,
                                                       u32 *value)
{
    u8 data[2];
    u16 code12;

    if (value == NULL) return DVC1124_CFG_ERR_VALUE;

    switch (field)
    {
    case DVC1124_CFG_EFF_COV_MV:
    case DVC1124_CFG_EFF_COV_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_COV_H, data, 2u)) return DVC1124_CFG_ERR_AFE_IO;
        code12 = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        *value = (field == DVC1124_CFG_EFF_COV_MV)
                     ? (code12 ? (u32)code12 + 500u : 0u)
                     : dvc_cfg_voltage_delay_ms(data[1]);
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_CUV_MV:
    case DVC1124_CFG_EFF_CUV_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_CUV_H, data, 2u)) return DVC1124_CFG_ERR_AFE_IO;
        code12 = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        *value = (field == DVC1124_CFG_EFF_CUV_MV)
                     ? code12
                     : dvc_cfg_voltage_delay_ms(data[1]);
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_OCD1_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD1_THR, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = data[0] ? dvc_cfg_current_x10_from_sense_uv((u32)data[0] * 250u) : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCD1_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD1_DLY, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 8u);
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC1_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC1_THR, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = data[0] ? dvc_cfg_current_x10_from_sense_uv((u32)data[0] * 250u) : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC1_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC1_DLY, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 8u);
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_OCD2_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD2, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (data[0] & DVC1124_OC2_ENABLE_MASK)
                     ? dvc_cfg_current_x10_from_sense_uv((u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u)
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCD2_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD2_DLY, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 4u);
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC2_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC2, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (data[0] & DVC1124_OC2_ENABLE_MASK)
                     ? dvc_cfg_current_x10_from_sense_uv((u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u)
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_OCC2_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC2_DLY, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (u16)((u16)(data[0] + 1u) * 4u);
        return DVC1124_CFG_OK;

    case DVC1124_CFG_EFF_SCD_MV:
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = (data[0] & DVC1124_SCD_ENABLE_MASK)
                     ? (u32)(data[0] & DVC1124_SCD_THRESHOLD_MASK) * 10u
                     : 0u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_EFF_SCD_DELAY_US:
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD_DLY, data, 1u)) return DVC1124_CFG_ERR_AFE_IO;
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
    dvc1124_persistent_config_t afe;

    if (value == NULL) return DVC1124_CFG_ERR_VALUE;

    switch (field)
    {
    case DVC1124_CFG_REQ_COV_MV:        *value = g_tParam.protect.u16VcellOvp_Third; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_COV_DELAY_MS:  *value = (u32)g_tParam.protect.u16VcellOvp_Filter * 10u; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_CUV_MV:        *value = g_tParam.protect.u16VcellUvp_Third; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_CUV_DELAY_MS:  *value = (u32)g_tParam.protect.u16VcellUvp_Filter * 10u; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD1_X10A:     *value = g_tParam.protect.u16IdsgOcp_First; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD1_DELAY_MS: *value = (u32)g_tParam.protect.u16IdsgOcp_Filter * 10u; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC1_X10A:     *value = g_tParam.protect.u16IchgOcp_First; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC1_DELAY_MS: *value = (u32)g_tParam.protect.u16IchgOcp_Filter * 10u; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD2_X10A:     *value = g_tParam.protect.u16IdsgOcp_Second; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD2_DELAY_MS: *value = (u32)g_tParam.protect.u16IdsgOcp_Filter * 10u; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC2_X10A:     *value = g_tParam.protect.u16IchgOcp_Second; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC2_DELAY_MS: *value = (u32)g_tParam.protect.u16IchgOcp_Filter * 10u; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_SCD_MV:
    case DVC1124_CFG_REQ_SCD_DELAY_US:
        if (dvc_cfg_load(&afe) != DVC1124_CFG_OK) return DVC1124_CFG_ERR_STORE;
        *value = (field == DVC1124_CFG_REQ_SCD_MV)
                     ? afe.scd_threshold_mv
                     : afe.scd_delay_us;
        return DVC1124_CFG_OK;
    default:
        return DVC1124_CFG_ERR_ADDRESS;
    }
}

dvc1124_config_result_t DVC1124_ConfigServiceRead(dvc1124_config_field_t field,
                                                   u32 *value)
{
    dvc1124_config_t device;
    dvc1124_snapshot_t snapshot;
    dvc1124_persistent_config_t cfg;
    u8 chip;

    if (value == NULL) return DVC1124_CFG_ERR_VALUE;

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
        if (!DVC1124_ReadRegisters(DVC1124_REG_CHIP_VERSION, &chip, 1u)) return DVC1124_CFG_ERR_AFE_IO;
        *value = chip; return DVC1124_CFG_OK;
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
    default: break;
    }

    if (dvc_cfg_load(&cfg) != DVC1124_CFG_OK) return DVC1124_CFG_ERR_STORE;

    switch (field)
    {
    case DVC1124_CFG_HS_FET_MASK:           *value = cfg.operating.high_side_fet_mask; break;
    case DVC1124_CFG_CADC_WORK_ENABLE:       *value = cfg.operating.cadc_work_enable; break;
    case DVC1124_CFG_CURRENT_WAKE_ENABLE:    *value = cfg.operating.current_wake_enable; break;
    case DVC1124_CFG_CC1_WORK_TIME:          *value = cfg.operating.cc1_work_time; break;
    case DVC1124_CFG_CC1_SLEEP_WAKE_TIME:    *value = cfg.operating.cc1_sleep_wake_time; break;
    case DVC1124_CFG_CHARGE_PUMP_VOLTAGE:    *value = cfg.operating.charge_pump_voltage ? (u32)cfg.operating.charge_pump_voltage + 5u : 0u; break;
    case DVC1124_CFG_CELL_MEAS_MASK:         *value = cfg.operating.cell_measurement_mask; break;
    case DVC1124_CFG_CELL_SIGNED_MODE:       *value = cfg.operating.cell_voltage_signed; break;
    case DVC1124_CFG_VADC_ENABLE:            *value = cfg.operating.vadc_enable; break;
    case DVC1124_CFG_VADC_SYNC:              *value = cfg.operating.vadc_sync_with_cc2; break;
    case DVC1124_CFG_VADC_PERIOD_CYCLES:     *value = 1u << (u8)cfg.operating.vadc_period; break;
    case DVC1124_CFG_VADC_TIME_US:           *value = dvc_cfg_vadc_time_us(cfg.operating.vadc_time); break;
    case DVC1124_CFG_GP1_MODE:               *value = cfg.operating.gp1_mode; break;
    case DVC1124_CFG_GP2_MODE:               *value = cfg.operating.gp2_mode; break;
    case DVC1124_CFG_GP3_MODE:               *value = cfg.operating.gp3_mode; break;
    case DVC1124_CFG_GP4_MODE:               *value = cfg.operating.gp4_mode; break;
    case DVC1124_CFG_GP5_MODE:               *value = cfg.operating.gp5_mode; break;
    case DVC1124_CFG_GP6_MODE:               *value = cfg.operating.gp6_mode; break;
    case DVC1124_CFG_V3P3_SLEEP_ENABLE:      *value = cfg.operating.v3p3_sleep_enable; break;
    case DVC1124_CFG_V3P3_WORK_ENABLE:       *value = cfg.operating.v3p3_work_enable; break;
    case DVC1124_CFG_V3P3_TIMEOUT_RESTART:   *value = cfg.operating.v3p3_timeout_restart; break;
    case DVC1124_CFG_I2C_WDT_SECONDS:        *value = dvc_cfg_wdt_seconds(cfg.operating.i2c_watchdog); break;
    case DVC1124_CFG_TIMED_WAKE_SECONDS:     *value = dvc_cfg_timed_wake_seconds(cfg.operating.timed_wake); break;
    case DVC1124_CFG_INTERRUPT_MASK:         *value = cfg.operating.interrupt_mask; break;
    case DVC1124_CFG_CURRENT_WAKE_UV:        *value = cfg.current_wake_threshold_uv; break;
    case DVC1124_CFG_BODY_DIODE_UV:          *value = cfg.body_diode_threshold_uv; break;
    case DVC1124_CFG_DSG_PULLDOWN:           *value = cfg.dsg_pulldown_strength; break;
    case DVC1124_CFG_I2C_TIMEOUT_CLOSE_CHG:  *value = cfg.i2c_timeout_close_chg; break;
    case DVC1124_CFG_I2C_TIMEOUT_CLOSE_DSG:  *value = cfg.i2c_timeout_close_dsg; break;
    case DVC1124_CFG_CORE_OT_X10C:           *value = dvc_cfg_core_ot_x10_from_code(cfg.core_ot_code); break;
    default: return DVC1124_CFG_ERR_ADDRESS;
    }

    return DVC1124_CFG_OK;
}

static dvc1124_config_result_t dvc_cfg_write_afe_field(dvc1124_config_field_t field,
                                                        u32 value)
{
    dvc1124_persistent_config_t before;
    dvc1124_persistent_config_t after;
    dvc1124_vadc_time_t vadc_time;
    dvc1124_i2c_wdt_code_t wdt;
    dvc1124_timed_wake_t timed;
    u8 code;

    if (dvc_cfg_load(&before) != DVC1124_CFG_OK) return DVC1124_CFG_ERR_STORE;
    after = before;

    switch (field)
    {
    case DVC1124_CFG_HS_FET_MASK: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.high_side_fet_mask = (u8)value; break;
    case DVC1124_CFG_CADC_WORK_ENABLE: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.cadc_work_enable = (u8)value; break;
    case DVC1124_CFG_CURRENT_WAKE_ENABLE: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.current_wake_enable = (u8)value; break;
    case DVC1124_CFG_CC1_WORK_TIME: if (value > 3u) return DVC1124_CFG_ERR_VALUE; after.operating.cc1_work_time = (dvc1124_cc1_work_time_t)value; break;
    case DVC1124_CFG_CC1_SLEEP_WAKE_TIME: if (value > 3u) return DVC1124_CFG_ERR_VALUE; after.operating.cc1_sleep_wake_time = (dvc1124_cc1_sleep_wake_time_t)value; break;
    case DVC1124_CFG_CHARGE_PUMP_VOLTAGE:
        if (value == 0u) after.operating.charge_pump_voltage = DVC1124_CPVS_OFF;
        else if (value >= 6u && value <= 12u) after.operating.charge_pump_voltage = (dvc1124_cp_voltage_t)(value - 5u);
        else return DVC1124_CFG_ERR_VALUE;
        break;
    case DVC1124_CFG_CELL_MEAS_MASK: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.cell_measurement_mask = (u8)value; break;
    case DVC1124_CFG_CELL_SIGNED_MODE: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.cell_voltage_signed = (u8)value; break;
    case DVC1124_CFG_VADC_ENABLE: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.vadc_enable = (u8)value; break;
    case DVC1124_CFG_VADC_SYNC: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.vadc_sync_with_cc2 = (u8)value; break;
    case DVC1124_CFG_VADC_PERIOD_CYCLES:
        if (value == 1u) after.operating.vadc_period = DVC1124_VADC_EVERY_1_CC2;
        else if (value == 2u) after.operating.vadc_period = DVC1124_VADC_EVERY_2_CC2;
        else if (value == 4u) after.operating.vadc_period = DVC1124_VADC_EVERY_4_CC2;
        else if (value == 8u) after.operating.vadc_period = DVC1124_VADC_EVERY_8_CC2;
        else return DVC1124_CFG_ERR_VALUE;
        break;
    case DVC1124_CFG_VADC_TIME_US:
        if (!dvc_cfg_vadc_time_code(value, &vadc_time)) return DVC1124_CFG_ERR_VALUE;
        after.operating.vadc_time = vadc_time;
        break;
    case DVC1124_CFG_GP1_MODE: if (value > 3u) return DVC1124_CFG_ERR_VALUE; after.operating.gp1_mode = (dvc1124_gp14_mode_t)value; break;
    case DVC1124_CFG_GP2_MODE:
    case DVC1124_CFG_GP3_MODE:
    case DVC1124_CFG_GP5_MODE:
    case DVC1124_CFG_GP6_MODE:
        if (!((value <= 2u) || value == 6u || value == 7u)) return DVC1124_CFG_ERR_VALUE;
        if (field == DVC1124_CFG_GP2_MODE) after.operating.gp2_mode = (dvc1124_gp236_mode_t)value;
        else if (field == DVC1124_CFG_GP3_MODE) after.operating.gp3_mode = (dvc1124_gp236_mode_t)value;
        else if (field == DVC1124_CFG_GP5_MODE) after.operating.gp5_mode = (dvc1124_gp236_mode_t)value;
        else after.operating.gp6_mode = (dvc1124_gp236_mode_t)value;
        break;
    case DVC1124_CFG_GP4_MODE: if (value > 3u) return DVC1124_CFG_ERR_VALUE; after.operating.gp4_mode = (dvc1124_gp14_mode_t)value; break;
    case DVC1124_CFG_V3P3_SLEEP_ENABLE: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.v3p3_sleep_enable = (u8)value; break;
    case DVC1124_CFG_V3P3_WORK_ENABLE: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.v3p3_work_enable = (u8)value; break;
    case DVC1124_CFG_V3P3_TIMEOUT_RESTART: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.v3p3_timeout_restart = (u8)value; break;
    case DVC1124_CFG_I2C_WDT_SECONDS:
        if (!dvc_cfg_wdt_code(value, &wdt)) return DVC1124_CFG_ERR_VALUE;
        after.operating.i2c_watchdog = wdt;
        break;
    case DVC1124_CFG_TIMED_WAKE_SECONDS:
        if (!dvc_cfg_timed_wake_code(value, &timed)) return DVC1124_CFG_ERR_VALUE;
        after.operating.timed_wake = timed;
        break;
    case DVC1124_CFG_INTERRUPT_MASK:
        if (value > 0xFFu) return DVC1124_CFG_ERR_VALUE;
        after.operating.interrupt_mask = (u8)value;
        break;
    case DVC1124_CFG_CURRENT_WAKE_UV:
        if (value > 0xFFFFu) return DVC1124_CFG_ERR_VALUE;
        after.current_wake_threshold_uv = (u16)value;
        break;
    case DVC1124_CFG_BODY_DIODE_UV:
        if (value > 0xFFFFu) return DVC1124_CFG_ERR_VALUE;
        after.body_diode_threshold_uv = (u16)value;
        break;
    case DVC1124_CFG_DSG_PULLDOWN:
        if (value > 30u) return DVC1124_CFG_ERR_VALUE;
        after.dsg_pulldown_strength = (u8)value;
        break;
    case DVC1124_CFG_I2C_TIMEOUT_CLOSE_CHG:
        if (value > 1u) return DVC1124_CFG_ERR_VALUE;
        after.i2c_timeout_close_chg = (u8)value;
        break;
    case DVC1124_CFG_I2C_TIMEOUT_CLOSE_DSG:
        if (value > 1u) return DVC1124_CFG_ERR_VALUE;
        after.i2c_timeout_close_dsg = (u8)value;
        break;
    case DVC1124_CFG_CORE_OT_X10C:
        if (!dvc_cfg_core_ot_code(value, &code)) return DVC1124_CFG_ERR_VALUE;
        after.core_ot_code = code;
        break;
    case DVC1124_CFG_REQ_SCD_MV:
        if (value > 0xFFFFu) return DVC1124_CFG_ERR_VALUE;
        after.scd_threshold_mv = (u16)value;
        break;
    case DVC1124_CFG_REQ_SCD_DELAY_US:
        if (value > 0xFFFFu) return DVC1124_CFG_ERR_VALUE;
        after.scd_delay_us = (u16)value;
        break;
    default:
        return DVC1124_CFG_ERR_ADDRESS;
    }

    return dvc_cfg_apply_store_transaction(&before, &after);
}

static dvc1124_config_result_t dvc_cfg_write_bms_protection(
    dvc1124_config_field_t field,
    u32 value)
{
    struct PRT_E2ROM_PARAS previous = g_tParam.protect;
    struct PRT_E2ROM_PARAS candidate = g_tParam.protect;
    dvc1124_config_t device;
    u32 sense_uv;

    if (value > 0xFFFFu) return DVC1124_CFG_ERR_VALUE;
    DVC1124_GetConfig(&device);
    if (device.shunt_uohm == 0u) return DVC1124_CFG_ERR_VALUE;

    switch (field)
    {
    case DVC1124_CFG_REQ_COV_MV:
        if (value != 0u && (value < 501u || value > 4595u)) return DVC1124_CFG_ERR_VALUE;
        candidate.u16VcellOvp_Third = (u16)value;
        break;
    case DVC1124_CFG_REQ_COV_DELAY_MS:
        if (value > 8000u) return DVC1124_CFG_ERR_VALUE;
        candidate.u16VcellOvp_Filter = (u16)((value + 5u) / 10u);
        break;
    case DVC1124_CFG_REQ_CUV_MV:
        if (value > 4095u) return DVC1124_CFG_ERR_VALUE;
        candidate.u16VcellUvp_Third = (u16)value;
        break;
    case DVC1124_CFG_REQ_CUV_DELAY_MS:
        if (value > 8000u) return DVC1124_CFG_ERR_VALUE;
        candidate.u16VcellUvp_Filter = (u16)((value + 5u) / 10u);
        break;
    case DVC1124_CFG_REQ_OCD1_X10A:
    case DVC1124_CFG_REQ_OCC1_X10A:
        sense_uv = value * device.shunt_uohm / 10u;
        if (sense_uv > 63750u) return DVC1124_CFG_ERR_VALUE;
        if (field == DVC1124_CFG_REQ_OCD1_X10A) candidate.u16IdsgOcp_First = (u16)value;
        else candidate.u16IchgOcp_First = (u16)value;
        break;
    case DVC1124_CFG_REQ_OCD2_X10A:
    case DVC1124_CFG_REQ_OCC2_X10A:
        sense_uv = value * device.shunt_uohm / 10u;
        if (sense_uv > 256000u) return DVC1124_CFG_ERR_VALUE;
        if (field == DVC1124_CFG_REQ_OCD2_X10A) candidate.u16IdsgOcp_Second = (u16)value;
        else candidate.u16IchgOcp_Second = (u16)value;
        break;
    case DVC1124_CFG_REQ_OCD1_DELAY_MS:
    case DVC1124_CFG_REQ_OCD2_DELAY_MS:
        if (value > 2048u) return DVC1124_CFG_ERR_VALUE;
        candidate.u16IdsgOcp_Filter = (u16)((value + 5u) / 10u);
        break;
    case DVC1124_CFG_REQ_OCC1_DELAY_MS:
    case DVC1124_CFG_REQ_OCC2_DELAY_MS:
        if (value > 2048u) return DVC1124_CFG_ERR_VALUE;
        candidate.u16IchgOcp_Filter = (u16)((value + 5u) / 10u);
        break;
    default:
        return DVC1124_CFG_ERR_ADDRESS;
    }

    g_tParam.protect = candidate;
    if (!DVC1124_ApplyProtectionConfig())
    {
        g_tParam.protect = previous;
        (void)DVC1124_ApplyProtectionConfig();
        return DVC1124_CFG_ERR_AFE_IO;
    }
    if (!bms_cold_kv_store_set_protect(&candidate))
    {
        g_tParam.protect = previous;
        (void)DVC1124_ApplyProtectionConfig();
        return DVC1124_CFG_ERR_STORE;
    }
    return DVC1124_CFG_OK;
}

dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t field,
                                                    u32 value)
{
    if ((u8)field >= (u8)DVC1124_CFG_EFF_COV_MV &&
        (u8)field <= (u8)DVC1124_CFG_EFF_SCD_DELAY_US)
        return DVC1124_CFG_ERR_READ_ONLY;

    switch (field)
    {
    case DVC1124_CFG_SCHEMA:
    case DVC1124_CFG_MODEL:
    case DVC1124_CFG_CHIP_VERSION:
    case DVC1124_CFG_WRITE_ADDR:
    case DVC1124_CFG_CELL_COUNT:
    case DVC1124_CFG_SHUNT_UOHM_LO:
    case DVC1124_CFG_SHUNT_UOHM_HI:
    case DVC1124_CFG_STATUS_CACHED:
    case DVC1124_CFG_CORE_OT_EVENT_LATCHED:
        return DVC1124_CFG_ERR_READ_ONLY;
    default:
        break;
    }

    if ((u8)field >= (u8)DVC1124_CFG_REQ_COV_MV &&
        (u8)field <= (u8)DVC1124_CFG_REQ_OCC2_DELAY_MS)
        return dvc_cfg_write_bms_protection(field, value);

    return dvc_cfg_write_afe_field(field, value);
}

dvc1124_config_result_t DVC1124_ConfigServiceReadRaw(u8 reg, u8 *value)
{
    if (value == NULL || reg > DVC1124_MAX_REGISTER) return DVC1124_CFG_ERR_ADDRESS;

    /*
     * Raw reads must not silently consume RC status. STATUS (0x01) and CORE_OT
     * (0x76) have read-clear fields in V1.2; use explicit semantic/cached
     * diagnostics instead of the ordinary raw mirror for those bytes.
     */
    if (DVC1124_RegReadHasSideEffect(reg)) return DVC1124_CFG_ERR_FORBIDDEN;

    return DVC1124_ReadRegisters(reg, value, 1u)
               ? DVC1124_CFG_OK
               : DVC1124_CFG_ERR_AFE_IO;
}

static dvc1124_config_result_t dvc_cfg_raw_to_candidate(
    u8 reg,
    u8 raw,
    dvc1124_persistent_config_t *cfg)
{
    u8 code;

    if (cfg == NULL) return DVC1124_CFG_ERR_VALUE;

    switch (reg)
    {
    case DVC1124_REG_DSG_PULLDOWN:
        cfg->dsg_pulldown_strength = (u8)(raw & DVC1124_DPC_MASK);
        break;
    case DVC1124_REG_DSG_MASK:
        cfg->i2c_timeout_close_dsg = (raw & DVC1124_DSGMASK_DWM_MASK) ? 0u : 1u;
        break;
    case DVC1124_REG_CHG_MASK:
        cfg->i2c_timeout_close_chg = (raw & DVC1124_CHGMASK_CWM_MASK) ? 0u : 1u;
        break;
    case DVC1124_REG_CADC_CTRL:
        cfg->operating.high_side_fet_mask = (raw & DVC1124_CADC_HSFM_MASK) ? 1u : 0u;
        cfg->operating.cadc_work_enable = (raw & DVC1124_CADC_CAEW_MASK) ? 1u : 0u;
        cfg->operating.current_wake_enable = (raw & DVC1124_CADC_CAES_MASK) ? 1u : 0u;
        break;
    case DVC1124_REG_CC1_TIMING:
        cfg->operating.cc1_work_time = (dvc1124_cc1_work_time_t)DVC1124_FIELD_GET(DVC1124_CC1_WORK_TIME_MASK, DVC1124_CC1_WORK_TIME_SHIFT, raw);
        cfg->operating.cc1_sleep_wake_time = (dvc1124_cc1_sleep_wake_time_t)DVC1124_FIELD_GET(DVC1124_CC1_SLEEP_WAKE_TIME_MASK, DVC1124_CC1_SLEEP_WAKE_TIME_SHIFT, raw);
        break;
    case DVC1124_REG_SCD:
        code = (u8)(raw & DVC1124_SCD_THRESHOLD_MASK);
        if ((raw & DVC1124_SCD_ENABLE_MASK) == 0u)
        {
            cfg->scd_threshold_mv = 0u;
            cfg->scd_delay_us = 0u;
        }
        else
        {
            if (code == 0u) return DVC1124_CFG_ERR_VALUE;
            cfg->scd_threshold_mv = (u16)code * 10u;
        }
        break;
    case DVC1124_REG_SCD_DLY:
        if (cfg->scd_threshold_mv == 0u && raw != 0u) return DVC1124_CFG_ERR_VALUE;
        cfg->scd_delay_us = (u16)(((u32)raw * 781u + 50u) / 100u);
        break;
    case DVC1124_REG_CURRENT_WAKE:
        cfg->current_wake_threshold_uv = (u16)raw * 10u;
        break;
    case DVC1124_REG_BODY_DIODE:
        cfg->body_diode_threshold_uv = (u16)raw * 40u;
        break;
    case DVC1124_REG_CP_CTRL:
        cfg->operating.charge_pump_voltage = (dvc1124_cp_voltage_t)DVC1124_FIELD_GET(DVC1124_CPVS_MASK, DVC1124_CPVS_SHIFT, raw);
        cfg->operating.cell_measurement_mask = (raw & DVC1124_CMM_MASK) ? 1u : 0u;
        cfg->operating.cell_voltage_signed = (raw & DVC1124_CVS_MASK) ? 1u : 0u;
        break;
    case DVC1124_REG_VADC_CTRL:
        cfg->operating.vadc_enable = (raw & DVC1124_VADC_ENABLE_MASK) ? 1u : 0u;
        cfg->operating.vadc_sync_with_cc2 = (raw & DVC1124_VADC_SYNC_MASK) ? 1u : 0u;
        cfg->operating.vadc_period = (dvc1124_vadc_period_t)DVC1124_FIELD_GET(DVC1124_VADC_PERIOD_MASK, DVC1124_VADC_PERIOD_SHIFT, raw);
        cfg->operating.vadc_time = (dvc1124_vadc_time_t)DVC1124_FIELD_GET(DVC1124_VADC_TIME_MASK, DVC1124_VADC_TIME_SHIFT, raw);
        break;
    case DVC1124_REG_GP123_MODE:
        cfg->operating.gp1_mode = (dvc1124_gp14_mode_t)DVC1124_FIELD_GET(DVC1124_GP1_MODE_MASK, DVC1124_GP1_MODE_SHIFT, raw);
        cfg->operating.gp2_mode = (dvc1124_gp236_mode_t)DVC1124_FIELD_GET(DVC1124_GP2_MODE_MASK, DVC1124_GP2_MODE_SHIFT, raw);
        cfg->operating.gp3_mode = (dvc1124_gp236_mode_t)DVC1124_FIELD_GET(DVC1124_GP3_MODE_MASK, DVC1124_GP3_MODE_SHIFT, raw);
        break;
    case DVC1124_REG_GP456_MODE:
        cfg->operating.gp4_mode = (dvc1124_gp14_mode_t)DVC1124_FIELD_GET(DVC1124_GP4_MODE_MASK, DVC1124_GP4_MODE_SHIFT, raw);
        cfg->operating.gp5_mode = (dvc1124_gp236_mode_t)DVC1124_FIELD_GET(DVC1124_GP5_MODE_MASK, DVC1124_GP5_MODE_SHIFT, raw);
        cfg->operating.gp6_mode = (dvc1124_gp236_mode_t)DVC1124_FIELD_GET(DVC1124_GP6_MODE_MASK, DVC1124_GP6_MODE_SHIFT, raw);
        break;
    case DVC1124_REG_CORE_OT:
        cfg->core_ot_code = (u8)(raw & DVC1124_CORE_OT_THRESHOLD_MASK);
        break;
    case DVC1124_REG_I2C_WDT:
        cfg->operating.v3p3_sleep_enable = (raw & DVC1124_V3P3_SLEEP_ENABLE_MASK) ? 1u : 0u;
        cfg->operating.v3p3_work_enable = (raw & DVC1124_V3P3_WORK_ENABLE_MASK) ? 1u : 0u;
        cfg->operating.v3p3_timeout_restart = (raw & DVC1124_V3P3_TIMEOUT_RESTART_MASK) ? 1u : 0u;
        code = (u8)(raw & DVC1124_I2C_WDT_TIME_MASK);
        cfg->operating.i2c_watchdog = (code >= DVC1124_I2C_WDT_4S)
                                           ? (dvc1124_i2c_wdt_code_t)code
                                           : DVC1124_I2C_WDT_OFF;
        break;
    case DVC1124_REG_TIMED_WAKE:
        cfg->operating.timed_wake = (dvc1124_timed_wake_t)(raw & DVC1124_TIMED_WAKE_TIME_MASK);
        break;
    case DVC1124_REG_INT_MASK:
        cfg->operating.interrupt_mask = raw;
        break;
    default:
        return DVC1124_CFG_ERR_FORBIDDEN;
    }

    return DVC1124_ConfigStoreValidate(cfg)
               ? DVC1124_CFG_OK
               : DVC1124_CFG_ERR_VALUE;
}

dvc1124_config_result_t DVC1124_ConfigServiceWriteRaw(u8 reg, u8 value)
{
    dvc1124_persistent_config_t before;
    dvc1124_persistent_config_t after;
    dvc1124_config_result_t result;

    if (Runtime_GetMode() != MODE_FACTORY) return DVC1124_CFG_ERR_FORBIDDEN;
    if (reg > DVC1124_MAX_REGISTER) return DVC1124_CFG_ERR_ADDRESS;

    if (dvc_cfg_load(&before) != DVC1124_CFG_OK) return DVC1124_CFG_ERR_STORE;
    after = before;

    result = dvc_cfg_raw_to_candidate(reg, value, &after);
    if (result != DVC1124_CFG_OK) return result;

    return dvc_cfg_apply_store_transaction(&before, &after);
}
