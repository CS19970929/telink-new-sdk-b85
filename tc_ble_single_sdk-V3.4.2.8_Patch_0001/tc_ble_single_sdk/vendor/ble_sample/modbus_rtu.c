#include "modbus_rtu.h"
// #include <string.h>
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "sci_upper.h"
#include "param.h"
#include "SocEnhance.h"
#include "bms_event_log.h"
#include "sh367309_datadeal.h"
#include "app.h"
#include "conf.h"
#include "runtime.h"
#include "dvc1124.h"

#include "stack/ble/ble.h"
#include "btname_modbus.h"

#define MB_ADDR 0x01
#define BMS_REALTIME_REG_BASE 0xD120u
#define BMS_REALTIME_REG_COUNT 11u
#define BMS_REALTIME_REG_MAGIC 0x4253u
#define BMS_REALTIME_REG_VERSION 0x0001u

#define BMS_REALTIME_REG_MAGIC_ADDR (BMS_REALTIME_REG_BASE + 0u)
#define BMS_REALTIME_REG_VERSION_ADDR (BMS_REALTIME_REG_BASE + 1u)
#define BMS_REALTIME_REG_VOLTAGE_ADDR (BMS_REALTIME_REG_BASE + 2u)
#define BMS_REALTIME_REG_CURRENT_ADDR (BMS_REALTIME_REG_BASE + 3u)
#define BMS_REALTIME_REG_SOC_ADDR (BMS_REALTIME_REG_BASE + 4u)
#define BMS_REALTIME_REG_TEMP_MAX_ADDR (BMS_REALTIME_REG_BASE + 5u)
#define BMS_REALTIME_REG_TEMP_MIN_ADDR (BMS_REALTIME_REG_BASE + 6u)
#define BMS_REALTIME_REG_TEMP_MOS_ADDR (BMS_REALTIME_REG_BASE + 7u)
#define BMS_REALTIME_REG_VCELL_MAX_ADDR (BMS_REALTIME_REG_BASE + 8u)
#define BMS_REALTIME_REG_VCELL_MIN_ADDR (BMS_REALTIME_REG_BASE + 9u)
#define BMS_REALTIME_REG_VCELL_DELTA_ADDR (BMS_REALTIME_REG_BASE + 10u)

static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset);
static u16 read_production_info_reg(u16 reg);
static int read_event_log_frame(u8 addr, u8 func, u16 reg, u16 qty, u8 *rsp, u32 *rsp_len);
static u16 read_realtime_status_reg(u16 reg);
static u16 encode_signed_current_reg(void);
static u16 read_dvc1124_comm_reg(u16 reg);
static void write_dvc1124_comm_reg(u16 reg, u16 val);
void WriteProID_Default(void);

struct stCell_Info g_stCellInfoReport;
PRODUCTION_ID_INFO ProductionInfor;

/* Runtime-only requested SCD values until the dedicated AFE NVM block lands. */
static u16 s_dvc1124_scd_req_mv = DVC1124_HW_SCD_THRESHOLD_MV;
static u16 s_dvc1124_scd_req_delay_us = DVC1124_HW_SCD_DELAY_US;

static u16 dvc_read_field(u8 reg, u8 mask, u8 shift)
{
    u8 value = 0u;
    if (!DVC1124_ReadRegisterField(reg, mask, shift, &value)) return 0xFFFFu;
    return value;
}

static u16 dvc_current_x10_from_sense_uv(u32 sense_uv)
{
    dvc1124_config_t cfg;
    u32 value;

    DVC1124_GetConfig(&cfg);
    if (cfg.shunt_uohm == 0u) return 0u;
    value = (sense_uv * 10u + cfg.shunt_uohm / 2u) / cfg.shunt_uohm;
    if (value > 0xFFFFu) value = 0xFFFFu;
    return (u16)value;
}

static u16 dvc_voltage_delay_ms(u8 code)
{
    static const u16 table_ms[16] = {
        200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
        1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
    };
    return table_ms[code & 0x0Fu];
}

static u16 dvc_vadc_time_us(u8 code)
{
    static const u16 table_us[4] = {790u, 1540u, 3030u, 6020u};
    return table_us[code & 0x03u];
}

static int dvc_vadc_time_code(u16 us, u8 *code)
{
    if (code == NULL) return 0;
    switch (us)
    {
    case 790u:  *code = DVC1124_VADC_TIME_0P79MS; return 1;
    case 1540u: *code = DVC1124_VADC_TIME_1P54MS; return 1;
    case 3030u: *code = DVC1124_VADC_TIME_3P03MS; return 1;
    case 6020u: *code = DVC1124_VADC_TIME_6P02MS; return 1;
    default: return 0;
    }
}

static u16 dvc_i2c_wdt_seconds(u8 code)
{
    switch (code & DVC1124_I2C_WDT_TIME_MASK)
    {
    case DVC1124_I2C_WDT_4S:  return 4u;
    case DVC1124_I2C_WDT_8S:  return 8u;
    case DVC1124_I2C_WDT_16S: return 16u;
    case DVC1124_I2C_WDT_32S: return 32u;
    default: return 0u;
    }
}

static int dvc_i2c_wdt_code(u16 seconds, u8 *code)
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

static u16 dvc_timed_wake_seconds(u8 code)
{
    static const u16 seconds[16] = {
        0u, 10u, 20u, 30u, 40u, 50u, 60u, 120u,
        180u, 240u, 300u, 360u, 420u, 480u, 540u, 600u
    };
    return seconds[code & 0x0Fu];
}

static int dvc_timed_wake_code(u16 seconds, u8 *code)
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
            *code = i;
            return 1;
        }
    }
    return 0;
}

static u16 dvc_core_ot_x10_from_code(u8 code)
{
    s32 value;
    if (code == 0u) return 0u;
    value = ((s32)(1466u + (u16)code * 2u) * 24467) / 10000 - 2710;
    if (value < 0) value = 0;
    if (value > 0xFFFF) value = 0xFFFF;
    return (u16)value;
}

static int dvc_core_ot_code_from_x10(u16 requested_x10, u8 *code)
{
    u8 i;
    u8 best = 0u;

    if (code == NULL) return 0;
    if (requested_x10 == 0u)
    {
        *code = 0u;
        return 1;
    }

    /* Safety policy: choose the highest supported threshold not above request. */
    for (i = 1u; i <= 127u; ++i)
    {
        u16 actual = dvc_core_ot_x10_from_code(i);
        if (actual <= requested_x10) best = i;
        else break;
    }
    if (best == 0u) return 0;
    *code = best;
    return 1;
}

static int dvc_write_persistent_raw(u8 dvc_reg, u8 requested)
{
    u8 current;
    u8 target;
    u8 verify;
    u8 mask = DVC1124_RegPersistentConfigMask(dvc_reg);

    if (mask == 0u) return 0;
    if (!DVC1124_ReadRegisters(dvc_reg, &current, 1u)) return 0;
    target = (u8)((current & (u8)~mask) | (requested & mask));
    if (!DVC1124_WriteRegisters(dvc_reg, &target, 1u)) return 0;
    if (!DVC1124_ReadRegisters(dvc_reg, &verify, 1u)) return 0;
    return ((verify & mask) == (target & mask)) ? 1 : 0;
}

static u16 dvc_read_effective_protection(u16 reg)
{
    u8 data[2];
    u16 code12;

    switch (reg)
    {
    case DVC1124_COMM_EFF_COV_MV:
    case DVC1124_COMM_EFF_COV_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_COV_H, data, 2u)) return 0xFFFFu;
        code12 = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        return (reg == DVC1124_COMM_EFF_COV_MV)
                   ? (code12 ? (u16)(code12 + 500u) : 0u)
                   : dvc_voltage_delay_ms(data[1]);

    case DVC1124_COMM_EFF_CUV_MV:
    case DVC1124_COMM_EFF_CUV_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_CUV_H, data, 2u)) return 0xFFFFu;
        code12 = (u16)(((u16)data[0] << 4) | (data[1] >> 4));
        return (reg == DVC1124_COMM_EFF_CUV_MV)
                   ? code12
                   : dvc_voltage_delay_ms(data[1]);

    case DVC1124_COMM_EFF_OCD1_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD1_THR, data, 1u)) return 0xFFFFu;
        return data[0] ? dvc_current_x10_from_sense_uv((u32)data[0] * 250u) : 0u;
    case DVC1124_COMM_EFF_OCD1_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD1_DLY, data, 1u)) return 0xFFFFu;
        return (u16)((u16)(data[0] + 1u) * 8u);
    case DVC1124_COMM_EFF_OCC1_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC1_THR, data, 1u)) return 0xFFFFu;
        return data[0] ? dvc_current_x10_from_sense_uv((u32)data[0] * 250u) : 0u;
    case DVC1124_COMM_EFF_OCC1_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC1_DLY, data, 1u)) return 0xFFFFu;
        return (u16)((u16)(data[0] + 1u) * 8u);

    case DVC1124_COMM_EFF_OCD2_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD2, data, 1u)) return 0xFFFFu;
        if ((data[0] & DVC1124_OC2_ENABLE_MASK) == 0u) return 0u;
        return dvc_current_x10_from_sense_uv((u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u);
    case DVC1124_COMM_EFF_OCD2_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCD2_DLY, data, 1u)) return 0xFFFFu;
        return (u16)((u16)(data[0] + 1u) * 4u);
    case DVC1124_COMM_EFF_OCC2_X10A:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC2, data, 1u)) return 0xFFFFu;
        if ((data[0] & DVC1124_OC2_ENABLE_MASK) == 0u) return 0u;
        return dvc_current_x10_from_sense_uv((u32)((data[0] & DVC1124_OC2_THRESHOLD_MASK) + 1u) * 4000u);
    case DVC1124_COMM_EFF_OCC2_DELAY_MS:
        if (!DVC1124_ReadRegisters(DVC1124_REG_OCC2_DLY, data, 1u)) return 0xFFFFu;
        return (u16)((u16)(data[0] + 1u) * 4u);

    case DVC1124_COMM_EFF_SCD_MV:
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD, data, 1u)) return 0xFFFFu;
        if ((data[0] & DVC1124_SCD_ENABLE_MASK) == 0u) return 0u;
        return (u16)((data[0] & DVC1124_SCD_THRESHOLD_MASK) * 10u);
    case DVC1124_COMM_EFF_SCD_DELAY_US:
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD_DLY, data, 1u)) return 0xFFFFu;
        return (u16)(((u32)data[0] * 781u + 50u) / 100u);
    default:
        return 0u;
    }
}

static u16 read_dvc1124_comm_reg(u16 reg)
{
    dvc1124_config_t cfg;
    u8 raw;
    u16 raw_offset;

    if (reg >= DVC1124_RAW_REG_BASE &&
        reg < (u16)(DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT))
    {
        raw_offset = (u16)(reg - DVC1124_RAW_REG_BASE);
        if (!DVC1124_ReadRegisters((u8)raw_offset, &raw, 1u)) return 0xFFFFu;
        return raw;
    }

    DVC1124_GetConfig(&cfg);
    switch (reg)
    {
    case DVC1124_COMM_SCHEMA:       return DVC1124_COMM_SCHEMA_VERSION;
    case DVC1124_COMM_MODEL:        return (u16)cfg.model;
    case DVC1124_COMM_CHIP_VERSION:
        if (!DVC1124_ReadRegisters(DVC1124_REG_CHIP_VERSION, &raw, 1u)) return 0xFFFFu;
        return raw;
    case DVC1124_COMM_WRITE_ADDR:   return DVC1124_GetWriteAddress();
    case DVC1124_COMM_CELL_COUNT:   return cfg.cell_count;
    case DVC1124_COMM_SHUNT_UOHM_LO:return (u16)(cfg.shunt_uohm & 0xFFFFu);
    case DVC1124_COMM_SHUNT_UOHM_HI:return (u16)(cfg.shunt_uohm >> 16);

    case DVC1124_COMM_HS_FET_MASK:          return dvc_read_field(DVC1124_REG_CADC_CTRL, DVC1124_CADC_HSFM_MASK, 7u);
    case DVC1124_COMM_CADC_WORK_ENABLE:     return dvc_read_field(DVC1124_REG_CADC_CTRL, DVC1124_CADC_CAEW_MASK, 3u);
    case DVC1124_COMM_CURRENT_WAKE_ENABLE:  return dvc_read_field(DVC1124_REG_CADC_CTRL, DVC1124_CADC_CAES_MASK, 2u);
    case DVC1124_COMM_CC1_WORK_TIME:        return dvc_read_field(DVC1124_REG_CC1_TIMING, DVC1124_CC1_WORK_TIME_MASK, DVC1124_CC1_WORK_TIME_SHIFT);
    case DVC1124_COMM_CC1_SLEEP_WAKE_TIME:  return dvc_read_field(DVC1124_REG_CC1_TIMING, DVC1124_CC1_SLEEP_WAKE_TIME_MASK, DVC1124_CC1_SLEEP_WAKE_TIME_SHIFT);
    case DVC1124_COMM_CHARGE_PUMP_VOLTAGE:
        raw = (u8)dvc_read_field(DVC1124_REG_CP_CTRL, DVC1124_CPVS_MASK, DVC1124_CPVS_SHIFT);
        return raw ? (u16)(raw + 5u) : 0u;
    case DVC1124_COMM_CELL_MEAS_MASK:        return dvc_read_field(DVC1124_REG_CP_CTRL, DVC1124_CMM_MASK, 1u);
    case DVC1124_COMM_CELL_SIGNED_MODE:      return dvc_read_field(DVC1124_REG_CP_CTRL, DVC1124_CVS_MASK, 0u);
    case DVC1124_COMM_VADC_ENABLE:           return dvc_read_field(DVC1124_REG_VADC_CTRL, DVC1124_VADC_ENABLE_MASK, 7u);
    case DVC1124_COMM_VADC_SYNC:             return dvc_read_field(DVC1124_REG_VADC_CTRL, DVC1124_VADC_SYNC_MASK, 6u);
    case DVC1124_COMM_VADC_PERIOD_CYCLES:
        raw = (u8)dvc_read_field(DVC1124_REG_VADC_CTRL, DVC1124_VADC_PERIOD_MASK, DVC1124_VADC_PERIOD_SHIFT);
        return (u16)(1u << raw);
    case DVC1124_COMM_VADC_TIME_US:
        raw = (u8)dvc_read_field(DVC1124_REG_VADC_CTRL, DVC1124_VADC_TIME_MASK, DVC1124_VADC_TIME_SHIFT);
        return dvc_vadc_time_us(raw);

    case DVC1124_COMM_GP1_MODE: return dvc_read_field(DVC1124_REG_GP123_MODE, DVC1124_GP1_MODE_MASK, DVC1124_GP1_MODE_SHIFT);
    case DVC1124_COMM_GP2_MODE: return dvc_read_field(DVC1124_REG_GP123_MODE, DVC1124_GP2_MODE_MASK, DVC1124_GP2_MODE_SHIFT);
    case DVC1124_COMM_GP3_MODE: return dvc_read_field(DVC1124_REG_GP123_MODE, DVC1124_GP3_MODE_MASK, DVC1124_GP3_MODE_SHIFT);
    case DVC1124_COMM_GP4_MODE: return dvc_read_field(DVC1124_REG_GP456_MODE, DVC1124_GP4_MODE_MASK, DVC1124_GP4_MODE_SHIFT);
    case DVC1124_COMM_GP5_MODE: return dvc_read_field(DVC1124_REG_GP456_MODE, DVC1124_GP5_MODE_MASK, DVC1124_GP5_MODE_SHIFT);
    case DVC1124_COMM_GP6_MODE: return dvc_read_field(DVC1124_REG_GP456_MODE, DVC1124_GP6_MODE_MASK, DVC1124_GP6_MODE_SHIFT);

    case DVC1124_COMM_V3P3_SLEEP_ENABLE:    return dvc_read_field(DVC1124_REG_I2C_WDT, DVC1124_V3P3_SLEEP_ENABLE_MASK, 7u);
    case DVC1124_COMM_V3P3_WORK_ENABLE:     return dvc_read_field(DVC1124_REG_I2C_WDT, DVC1124_V3P3_WORK_ENABLE_MASK, 6u);
    case DVC1124_COMM_V3P3_TIMEOUT_RESTART: return dvc_read_field(DVC1124_REG_I2C_WDT, DVC1124_V3P3_TIMEOUT_RESTART_MASK, 5u);
    case DVC1124_COMM_I2C_WDT_SECONDS:
        raw = (u8)dvc_read_field(DVC1124_REG_I2C_WDT, DVC1124_I2C_WDT_TIME_MASK, DVC1124_I2C_WDT_TIME_SHIFT);
        return dvc_i2c_wdt_seconds(raw);
    case DVC1124_COMM_TIMED_WAKE_SECONDS:
        raw = (u8)dvc_read_field(DVC1124_REG_TIMED_WAKE, DVC1124_TIMED_WAKE_TIME_MASK, DVC1124_TIMED_WAKE_TIME_SHIFT);
        return dvc_timed_wake_seconds(raw);
    case DVC1124_COMM_INTERRUPT_MASK:
        if (!DVC1124_ReadRegisters(DVC1124_REG_INT_MASK, &raw, 1u)) return 0xFFFFu;
        return raw;
    case DVC1124_COMM_CURRENT_WAKE_UV:
        if (!DVC1124_ReadRegisters(DVC1124_REG_CURRENT_WAKE, &raw, 1u)) return 0xFFFFu;
        return (u16)raw * 10u;
    case DVC1124_COMM_BODY_DIODE_UV:
        if (!DVC1124_ReadRegisters(DVC1124_REG_BODY_DIODE, &raw, 1u)) return 0xFFFFu;
        return (u16)raw * 40u;
    case DVC1124_COMM_DSG_PULLDOWN:
        return dvc_read_field(DVC1124_REG_DSG_PULLDOWN, DVC1124_DPC_MASK, DVC1124_DPC_SHIFT);
    case DVC1124_COMM_I2C_TIMEOUT_CLOSE_CHG:
        raw = (u8)dvc_read_field(DVC1124_REG_CHG_MASK, DVC1124_CHGMASK_CWM_MASK, 7u);
        return raw ? 0u : 1u;
    case DVC1124_COMM_I2C_TIMEOUT_CLOSE_DSG:
        raw = (u8)dvc_read_field(DVC1124_REG_DSG_MASK, DVC1124_DSGMASK_DWM_MASK, 3u);
        return raw ? 0u : 1u;
    case DVC1124_COMM_CORE_OT_X10C:
        raw = (u8)dvc_read_field(DVC1124_REG_CORE_OT, DVC1124_CORE_OT_THRESHOLD_MASK, DVC1124_CORE_OT_THRESHOLD_SHIFT);
        return dvc_core_ot_x10_from_code(raw);

    case DVC1124_COMM_REQ_COV_MV:        return g_tParam.protect.u16VcellOvp_Third;
    case DVC1124_COMM_REQ_COV_DELAY_MS:  return (u16)(g_tParam.protect.u16VcellOvp_Filter * 10u);
    case DVC1124_COMM_REQ_CUV_MV:        return g_tParam.protect.u16VcellUvp_Third;
    case DVC1124_COMM_REQ_CUV_DELAY_MS:  return (u16)(g_tParam.protect.u16VcellUvp_Filter * 10u);
    case DVC1124_COMM_REQ_OCD1_X10A:     return g_tParam.protect.u16IdsgOcp_First;
    case DVC1124_COMM_REQ_OCD1_DELAY_MS: return (u16)(g_tParam.protect.u16IdsgOcp_Filter * 10u);
    case DVC1124_COMM_REQ_OCC1_X10A:     return g_tParam.protect.u16IchgOcp_First;
    case DVC1124_COMM_REQ_OCC1_DELAY_MS: return (u16)(g_tParam.protect.u16IchgOcp_Filter * 10u);
    case DVC1124_COMM_REQ_OCD2_X10A:     return g_tParam.protect.u16IdsgOcp_Second;
    case DVC1124_COMM_REQ_OCD2_DELAY_MS: return (u16)(g_tParam.protect.u16IdsgOcp_Filter * 10u);
    case DVC1124_COMM_REQ_OCC2_X10A:     return g_tParam.protect.u16IchgOcp_Second;
    case DVC1124_COMM_REQ_OCC2_DELAY_MS: return (u16)(g_tParam.protect.u16IchgOcp_Filter * 10u);
    case DVC1124_COMM_REQ_SCD_MV:        return s_dvc1124_scd_req_mv;
    case DVC1124_COMM_REQ_SCD_DELAY_US:  return s_dvc1124_scd_req_delay_us;

    case DVC1124_COMM_EFF_COV_MV:
    case DVC1124_COMM_EFF_COV_DELAY_MS:
    case DVC1124_COMM_EFF_CUV_MV:
    case DVC1124_COMM_EFF_CUV_DELAY_MS:
    case DVC1124_COMM_EFF_OCD1_X10A:
    case DVC1124_COMM_EFF_OCD1_DELAY_MS:
    case DVC1124_COMM_EFF_OCC1_X10A:
    case DVC1124_COMM_EFF_OCC1_DELAY_MS:
    case DVC1124_COMM_EFF_OCD2_X10A:
    case DVC1124_COMM_EFF_OCD2_DELAY_MS:
    case DVC1124_COMM_EFF_OCC2_X10A:
    case DVC1124_COMM_EFF_OCC2_DELAY_MS:
    case DVC1124_COMM_EFF_SCD_MV:
    case DVC1124_COMM_EFF_SCD_DELAY_US:
        return dvc_read_effective_protection(reg);
    default:
        return 0u;
    }
}

static void write_dvc1124_comm_reg(u16 reg, u16 val)
{
    u8 code;
    u16 raw_offset;

    if (reg >= DVC1124_RAW_REG_BASE &&
        reg < (u16)(DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT))
    {
        raw_offset = (u16)(reg - DVC1124_RAW_REG_BASE);
        if ((val <= 0xFFu) && (DVC1124_RegPersistentConfigMask((u8)raw_offset) != 0u))
            (void)dvc_write_persistent_raw((u8)raw_offset, (u8)val);
        return;
    }

    switch (reg)
    {
    case DVC1124_COMM_CELL_COUNT:
        if (val >= DVC1124_MIN_CELLS && val <= DVC1124_MAX_CELLS)
            (void)DVC1124_SetCellCount((u8)val);
        break;

    case DVC1124_COMM_HS_FET_MASK:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CADC_CTRL, DVC1124_CADC_HSFM_MASK, 7u, (u8)val);
        break;
    case DVC1124_COMM_CADC_WORK_ENABLE:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CADC_CTRL, DVC1124_CADC_CAEW_MASK, 3u, (u8)val);
        break;
    case DVC1124_COMM_CURRENT_WAKE_ENABLE:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CADC_CTRL, DVC1124_CADC_CAES_MASK, 2u, (u8)val);
        break;
    case DVC1124_COMM_CC1_WORK_TIME:
        if (val <= 3u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CC1_TIMING, DVC1124_CC1_WORK_TIME_MASK, DVC1124_CC1_WORK_TIME_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_CC1_SLEEP_WAKE_TIME:
        if (val <= 3u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CC1_TIMING, DVC1124_CC1_SLEEP_WAKE_TIME_MASK, DVC1124_CC1_SLEEP_WAKE_TIME_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_CHARGE_PUMP_VOLTAGE:
        if (val == 0u) code = 0u;
        else if (val >= 6u && val <= 12u) code = (u8)(val - 5u);
        else break;
        (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CP_CTRL, DVC1124_CPVS_MASK, DVC1124_CPVS_SHIFT, code);
        break;
    case DVC1124_COMM_CELL_MEAS_MASK:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CP_CTRL, DVC1124_CMM_MASK, 1u, (u8)val);
        break;
    case DVC1124_COMM_CELL_SIGNED_MODE:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CP_CTRL, DVC1124_CVS_MASK, 0u, (u8)val);
        break;
    case DVC1124_COMM_VADC_ENABLE:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_VADC_CTRL, DVC1124_VADC_ENABLE_MASK, 7u, (u8)val);
        break;
    case DVC1124_COMM_VADC_SYNC:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_VADC_CTRL, DVC1124_VADC_SYNC_MASK, 6u, (u8)val);
        break;
    case DVC1124_COMM_VADC_PERIOD_CYCLES:
        if (val == 1u) code = 0u;
        else if (val == 2u) code = 1u;
        else if (val == 4u) code = 2u;
        else if (val == 8u) code = 3u;
        else break;
        (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_VADC_CTRL, DVC1124_VADC_PERIOD_MASK, DVC1124_VADC_PERIOD_SHIFT, code);
        break;
    case DVC1124_COMM_VADC_TIME_US:
        if (dvc_vadc_time_code(val, &code))
            (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_VADC_CTRL, DVC1124_VADC_TIME_MASK, DVC1124_VADC_TIME_SHIFT, code);
        break;

    case DVC1124_COMM_GP1_MODE:
        if (val <= 3u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_GP123_MODE, DVC1124_GP1_MODE_MASK, DVC1124_GP1_MODE_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_GP2_MODE:
        if ((val <= 2u) || (val >= 6u && val <= 7u)) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_GP123_MODE, DVC1124_GP2_MODE_MASK, DVC1124_GP2_MODE_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_GP3_MODE:
        if ((val <= 2u) || (val >= 6u && val <= 7u)) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_GP123_MODE, DVC1124_GP3_MODE_MASK, DVC1124_GP3_MODE_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_GP4_MODE:
        if (val <= 3u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_GP456_MODE, DVC1124_GP4_MODE_MASK, DVC1124_GP4_MODE_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_GP5_MODE:
        if ((val <= 2u) || (val >= 6u && val <= 7u)) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_GP456_MODE, DVC1124_GP5_MODE_MASK, DVC1124_GP5_MODE_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_GP6_MODE:
        if ((val <= 2u) || (val >= 6u && val <= 7u)) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_GP456_MODE, DVC1124_GP6_MODE_MASK, DVC1124_GP6_MODE_SHIFT, (u8)val);
        break;

    case DVC1124_COMM_V3P3_SLEEP_ENABLE:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_I2C_WDT, DVC1124_V3P3_SLEEP_ENABLE_MASK, 7u, (u8)val);
        break;
    case DVC1124_COMM_V3P3_WORK_ENABLE:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_I2C_WDT, DVC1124_V3P3_WORK_ENABLE_MASK, 6u, (u8)val);
        break;
    case DVC1124_COMM_V3P3_TIMEOUT_RESTART:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_I2C_WDT, DVC1124_V3P3_TIMEOUT_RESTART_MASK, 5u, (u8)val);
        break;
    case DVC1124_COMM_I2C_WDT_SECONDS:
        if (dvc_i2c_wdt_code(val, &code))
            (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_I2C_WDT, DVC1124_I2C_WDT_TIME_MASK, DVC1124_I2C_WDT_TIME_SHIFT, code);
        break;
    case DVC1124_COMM_TIMED_WAKE_SECONDS:
        if (dvc_timed_wake_code(val, &code))
            (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_TIMED_WAKE, DVC1124_TIMED_WAKE_TIME_MASK, DVC1124_TIMED_WAKE_TIME_SHIFT, code);
        break;
    case DVC1124_COMM_INTERRUPT_MASK:
        if (val <= 0xFFu) (void)dvc_write_persistent_raw(DVC1124_REG_INT_MASK, (u8)val);
        break;
    case DVC1124_COMM_CURRENT_WAKE_UV:
        if (val == 0u || ((val >= 10u) && (val <= 2550u) && ((val % 10u) == 0u)))
        {
            code = (u8)(val / 10u);
            (void)dvc_write_persistent_raw(DVC1124_REG_CURRENT_WAKE, code);
        }
        break;
    case DVC1124_COMM_BODY_DIODE_UV:
        if (val == 0u || ((val >= 40u) && (val <= 10200u) && ((val % 40u) == 0u)))
        {
            code = (u8)(val / 40u);
            (void)dvc_write_persistent_raw(DVC1124_REG_BODY_DIODE, code);
        }
        break;
    case DVC1124_COMM_DSG_PULLDOWN:
        if (val <= 30u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_PULLDOWN, DVC1124_DPC_MASK, DVC1124_DPC_SHIFT, (u8)val);
        break;
    case DVC1124_COMM_I2C_TIMEOUT_CLOSE_CHG:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CHG_MASK, DVC1124_CHGMASK_CWM_MASK, 7u, val ? 0u : 1u);
        break;
    case DVC1124_COMM_I2C_TIMEOUT_CLOSE_DSG:
        if (val <= 1u) (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_MASK, DVC1124_DSGMASK_DWM_MASK, 3u, val ? 0u : 1u);
        break;
    case DVC1124_COMM_CORE_OT_X10C:
        if (dvc_core_ot_code_from_x10(val, &code))
            (void)DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CORE_OT, DVC1124_CORE_OT_THRESHOLD_MASK, DVC1124_CORE_OT_THRESHOLD_SHIFT, code);
        break;

    /* Requested protection aliases. Existing BMS parameter storage remains the source of truth. */
    case DVC1124_COMM_REQ_COV_MV:        g_tParam.protect.u16VcellOvp_Third = val; break;
    case DVC1124_COMM_REQ_COV_DELAY_MS:  g_tParam.protect.u16VcellOvp_Filter = (u16)((val + 5u) / 10u); break;
    case DVC1124_COMM_REQ_CUV_MV:        g_tParam.protect.u16VcellUvp_Third = val; break;
    case DVC1124_COMM_REQ_CUV_DELAY_MS:  g_tParam.protect.u16VcellUvp_Filter = (u16)((val + 5u) / 10u); break;
    case DVC1124_COMM_REQ_OCD1_X10A:     g_tParam.protect.u16IdsgOcp_First = val; break;
    case DVC1124_COMM_REQ_OCD1_DELAY_MS: g_tParam.protect.u16IdsgOcp_Filter = (u16)((val + 5u) / 10u); break;
    case DVC1124_COMM_REQ_OCC1_X10A:     g_tParam.protect.u16IchgOcp_First = val; break;
    case DVC1124_COMM_REQ_OCC1_DELAY_MS: g_tParam.protect.u16IchgOcp_Filter = (u16)((val + 5u) / 10u); break;
    case DVC1124_COMM_REQ_OCD2_X10A:     g_tParam.protect.u16IdsgOcp_Second = val; break;
    case DVC1124_COMM_REQ_OCD2_DELAY_MS: g_tParam.protect.u16IdsgOcp_Filter = (u16)((val + 5u) / 10u); break;
    case DVC1124_COMM_REQ_OCC2_X10A:     g_tParam.protect.u16IchgOcp_Second = val; break;
    case DVC1124_COMM_REQ_OCC2_DELAY_MS: g_tParam.protect.u16IchgOcp_Filter = (u16)((val + 5u) / 10u); break;
    case DVC1124_COMM_REQ_SCD_MV:
        s_dvc1124_scd_req_mv = val;
        (void)DVC1124_SetShortCircuitProtection(s_dvc1124_scd_req_mv, s_dvc1124_scd_req_delay_us);
        break;
    case DVC1124_COMM_REQ_SCD_DELAY_US:
        s_dvc1124_scd_req_delay_us = val;
        (void)DVC1124_SetShortCircuitProtection(s_dvc1124_scd_req_mv, s_dvc1124_scd_req_delay_us);
        break;
    default:
        break;
    }
}

static u16 read_reg(u16 reg)
{
    u16 val;

    if ((reg >= DVC1124_COMM_REG_BASE &&
         reg < (u16)(DVC1124_COMM_REG_BASE + DVC1124_COMM_REG_COUNT)) ||
        (reg >= DVC1124_RAW_REG_BASE &&
         reg < (u16)(DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT)))
    {
        return read_dvc1124_comm_reg(reg);
    }

    if (reg < 3u)
    {
        switch (reg)
        {
        case 0:
            val = (g_stCellInfoReport.mac_public[0] << 8) | g_stCellInfoReport.mac_public[1];
            break;
        case 1:
            val = (g_stCellInfoReport.mac_public[2] << 8) | g_stCellInfoReport.mac_public[3];
            break;
        case 2:
            val = (g_stCellInfoReport.mac_public[4] << 8) | g_stCellInfoReport.mac_public[5];
            break;
        default:
            val = 0u;
            break;
        }
        return val;
    }

    if (reg >= BTNAME_REG_BASE && reg < BTNAME_REG_BASE + BTNAME_REG_COUNT)
    {
        u16 idx = reg - BTNAME_REG_BASE;
        u16 str_idx = idx * 2;
        const char *name = btname_get();
        u8 high_byte = 0x00;
        u8 low_byte = 0x00;

        if (str_idx < BTNAME_TOTAL_MAX_LEN && name[str_idx] != '\0') high_byte = name[str_idx];
        if (str_idx + 1 < BTNAME_TOTAL_MAX_LEN && name[str_idx + 1] != '\0') low_byte = name[str_idx + 1];
        return (high_byte << 8) | low_byte;
    }

    if (reg >= 0xc002 && reg <= (0xc002 + 48))
    {
        return read_production_info_reg(reg);
    }

    if (reg >= 0xd000 && reg <= 0xd03e)
    {
        return *(&g_stCellInfoReport.u16VCell[0] + (reg - 0xd000));
    }
    if (reg >= 0x2100 && reg <= 0x2140)
    {
        return *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100));
    }
    if (reg >= 0xD100 && reg <= 0xD114)
    {
        UINT16 u16SciTemp;
        UINT16 j;
        INT8 k;
        UINT8 a[4];

        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_First2 - 1 - j;
            if (k < 0) k = Record_len + k;
            a[j] = k;
        }
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_Second2 - 1 - j;
            if (k < 0) k = Record_len + k;
            a[j] = k;
        }
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_Third2 - 1 - j;
            if (k < 0) k = Record_len + k;
            a[j] = k;
        }

        switch (reg)
        {
        case 0xD100:
        case 0xD101:
        case 0xD102:
            return 0;
        case 0xD103:
            u16SciTemp = (Fault_record_First2[a[0]] << 8) | Fault_record_First2[a[1]];
            return u16SciTemp;
        case 0xD104:
            u16SciTemp = (Fault_record_First2[a[2]] << 8) | Fault_record_First2[a[3]];
            return u16SciTemp;
        case 0xD105:
            u16SciTemp = (Fault_record_Second2[a[0]] << 8) | Fault_record_Second2[a[1]];
            return u16SciTemp;
        case 0xD106:
            u16SciTemp = (Fault_record_Second2[a[2]] << 8) | Fault_record_Second2[a[3]];
            return u16SciTemp;
        case 0xD107:
            u16SciTemp = (Fault_record_Third2[a[0]] << 8) | Fault_record_Third2[a[1]];
            return u16SciTemp;
        case 0xD108:
            u16SciTemp = (Fault_record_Third2[a[2]] << 8) | Fault_record_Third2[a[3]];
            return u16SciTemp;
        default:
            break;
        }

        if (reg >= 0xD109 && reg <= 0xD114)
        {
            return ((*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * (reg - 0xd109))) << 8) |
                   (*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * (reg - 0xd109) + 1));
        }
    }
    if (reg >= 0xD115 && reg <= 0xD118)
    {
        if (reg == 0xd115) return ((UINT16)(SystemStatus.all & 0x0000FFFF));
        if (reg == 0xd116) return ((UINT16)(SystemStatus.all >> 16));
    }
    if (reg >= BMS_REALTIME_REG_BASE && reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT))
    {
        return read_realtime_status_reg(reg);
    }
    return 0;
}

extern bool deepsleep_en;
extern uint8_t get_soc_real(void);
static int reg_requires_param_save(u16 reg)
{
    if (reg >= 0x2100u && reg <= 0x2140u) return 1;
    return (reg >= DVC1124_COMM_REQ_COV_MV && reg <= DVC1124_COMM_REQ_OCC2_DELAY_MS);
}

static void write_reg(u16 reg, u16 val)
{
    if ((reg >= DVC1124_COMM_REG_BASE &&
         reg < (u16)(DVC1124_COMM_REG_BASE + DVC1124_COMM_REG_COUNT)) ||
        (reg >= DVC1124_RAW_REG_BASE &&
         reg < (u16)(DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT)))
    {
        write_dvc1124_comm_reg(reg, val);
        return;
    }

    if (reg >= 0x2100 && reg <= 0x2140)
    {
        *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100)) = val;
    }
    if (reg == 0x1005)
        set_soc_param(val, 1, 1);
    if (reg == 0x1102)
    {
        if (val == 0x03)
        {
            if (!Runtime_ReenterFactoryMode())
            {
                System_ERROR_UserCallback(ERROR_EEPROM_STORE);
            }
        }
#ifdef __TEST_SOC__
        if (val == 0x01)
        {
            sys_time.CHG = CapacityFactory * 5;
            sys_time.DSG = 0;
        }
#endif
        if (val == 0x0A)
            deepsleep_en = true;
    }
    if (reg == 0x1103)
    {
#ifdef __TEST_SOC__
        if(val == 0x01)
        {
            sys_time.CHG = 0;
            sys_time.DSG = CapacityFactory * 5;
        }
#endif
    }
    if (reg == 0x2319)
    {
        SOC_Calculate_Element.u32Cycle_times = val;
        set_soc_param(get_soc_real(), 1, 1);
    }
    if ((reg == BMS_EVENT_LOG_RESET_REG) && (val == 0x0001u))
    {
        (void)bms_event_log_factory_reset();
    }
}

u16 mb_crc16(const u8 *buf, u32 len)
{
    u16 crc = 0xFFFF;
    for (u32 i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (u8 j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static u16 u16be(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static void put_u16be(u8 *p, u16 v)
{
    p[0] = v >> 8;
    p[1] = v & 0xFF;
}

extern int AFE_PARAM_WRITE_Flag;

int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    *rsp_len = 0;

    if (req_len < 4)
        return 0;
    if (req[0] != MB_ADDR && req[0] != 0x00)
        return 0;

    u16 crc_rx = ((u16)req[req_len - 1] << 8) | req[req_len - 2];
    u16 crc = mb_crc16(req, req_len - 2);
    if (crc != crc_rx)
    {
        return 0;
    }

    u8 addr = req[0];
    u8 func = req[1];

    if (func == 0x7F && addr != 0x00)
    {
        if (req_len <= 268)
        {
            memcpy(rsp, req, req_len);
            *rsp_len = req_len;
            return 1;
        }
        return 0;
    }

    if (func == 0x03)
    {
        if (req_len < 8)
            return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        if (qty == 0 || qty > 0x7D)
            return 0;

        if (read_event_log_frame(addr, func, reg, qty, rsp, rsp_len))
        {
            return (addr != 0x00);
        }
        u32 bytes = qty * 2;
        rsp[0] = addr;
        rsp[1] = func;
        rsp[2] = (u8)bytes;
        for (u16 i = 0; i < qty; i++)
        {
            u16 v = read_reg(reg + i);
            put_u16be(&rsp[3 + i * 2], v);
        }
        u32 l = 3 + bytes;
        u16 c = mb_crc16(rsp, l);
        rsp[l + 0] = (u8)(c & 0xFF);
        rsp[l + 1] = (u8)(c >> 8);
        *rsp_len = l + 2;
        return (addr != 0x00);
    }
    else if (func == 0x06)
    {
        if (req_len < 8)
            return 0;
        u16 reg = u16be(&req[2]);
        u16 val = u16be(&req[4]);
        write_reg(reg, val);
        if (reg_requires_param_save(reg))
        {
            SaveParam();
            AFE_PARAM_WRITE_Flag = 1;
        }

        if (addr == 0x00)
            return 0;
        memcpy(rsp, req, req_len);
        *rsp_len = req_len;
        return 1;
    }
    else if (func == 0x10)
    {
        if (req_len < 9)
            return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        u8 bytecnt = req[6];
        int need_save_param = 0;
        if (qty == 0 || qty > 0x7B)
            return 0;
        if (bytecnt != qty * 2)
            return 0;
        if (req_len < (u32)(7 + bytecnt + 2))
            return 0;

        const u8 *pdata = &req[7];
        for (u16 i = 0; i < qty; i++)
        {
            u16 v = u16be(&pdata[i * 2]);
            write_reg(reg + i, v);
            if (reg_requires_param_save((u16)(reg + i)))
            {
                need_save_param = 1;
            }
        }
        if (need_save_param)
        {
            SaveParam();
            AFE_PARAM_WRITE_Flag = 1;
        }

        if (reg >= BTNAME_REG_BASE && reg < (BTNAME_REG_BASE + BTNAME_REG_WORDS))
        {
            btname_modbus_on_write_holding(addr, qty, (const uint16_t *)pdata);
        }

        if (addr == 0x00)
            return 0;
        rsp[0] = addr;
        rsp[1] = func;
        put_u16be(&rsp[2], reg);
        put_u16be(&rsp[4], qty);
        u16 c = mb_crc16(rsp, 6);
        rsp[6] = (u8)(c & 0xFF);
        rsp[7] = (u8)(c >> 8);
        *rsp_len = 8;
        return 1;
    }

    if (addr == 0x00)
        return 0;
    rsp[0] = addr;
    rsp[1] = func | 0x80;
    rsp[2] = 0x01;
    u16 c = mb_crc16(rsp, 3);
    rsp[3] = (u8)(c & 0xFF);
    rsp[4] = (u8)(c >> 8);
    *rsp_len = 5;
    return 1;
}

static int read_event_log_frame(u8 addr, u8 func, u16 reg, u16 qty, u8 *rsp, u32 *rsp_len)
{
    u16 i;
    u32 bytes;
    u32 l;
    u16 c;

    if (reg != BMS_EVENT_LOG_REG_BASE)
    {
        return 0;
    }

    if ((qty == 0u) || (qty > BMS_EVENT_LOG_REG_COUNT))
    {
        return 0;
    }

    bytes = (u32)qty * 2u;
    rsp[0] = addr;
    rsp[1] = func;
    rsp[2] = (u8)bytes;
    for (i = 0u; i < qty; ++i)
    {
        u16 v = bms_event_log_read_reg(i);
        put_u16be(&rsp[3 + i * 2u], v);
    }

    l = 3u + bytes;
    c = mb_crc16(rsp, l);
    rsp[l + 0u] = (u8)(c & 0xFFu);
    rsp[l + 1u] = (u8)(c >> 8);
    *rsp_len = l + 2u;
    return 1;
}

static u16 encode_signed_current_reg(void)
{
    int16_t signed_current = 0;

    if (g_stCellInfoReport.u16IDischg)
    {
        signed_current = (int16_t)(-((int16_t)g_stCellInfoReport.u16IDischg));
    }
    else if (g_stCellInfoReport.u16Ichg)
    {
        signed_current = (int16_t)g_stCellInfoReport.u16Ichg;
    }

    return (u16)signed_current;
}

static u16 read_realtime_status_reg(u16 reg)
{
    switch (reg)
    {
    case BMS_REALTIME_REG_MAGIC_ADDR:
        return BMS_REALTIME_REG_MAGIC;
    case BMS_REALTIME_REG_VERSION_ADDR:
        return BMS_REALTIME_REG_VERSION;
    case BMS_REALTIME_REG_VOLTAGE_ADDR:
        return g_stCellInfoReport.u16VCellTotle;
    case BMS_REALTIME_REG_CURRENT_ADDR:
        return encode_signed_current_reg();
    case BMS_REALTIME_REG_SOC_ADDR:
        return g_stCellInfoReport.SocElement.u16Soc;
    case BMS_REALTIME_REG_TEMP_MAX_ADDR:
        return g_stCellInfoReport.u16TempMax;
    case BMS_REALTIME_REG_TEMP_MIN_ADDR:
        return g_stCellInfoReport.u16TempMin;
    case BMS_REALTIME_REG_TEMP_MOS_ADDR:
        return g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    case BMS_REALTIME_REG_VCELL_MAX_ADDR:
        return g_stCellInfoReport.u16VCellMax;
    case BMS_REALTIME_REG_VCELL_MIN_ADDR:
        return g_stCellInfoReport.u16VCellMin;
    case BMS_REALTIME_REG_VCELL_DELTA_ADDR:
        return g_stCellInfoReport.u16VCellDelta;
    default:
        return 0;
    }
}

static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset)
{
    u16 str_idx = reg_offset * 2;
    u8 high_byte = 0x00;
    u8 low_byte = 0x00;

    if (str_idx < max_len && str[str_idx] != '\0')
    {
        high_byte = str[str_idx];
    }

    if ((str_idx + 1) < max_len && str[str_idx + 1] != '\0')
    {
        low_byte = str[str_idx + 1];
    }

    return ((u16)high_byte << 8) | low_byte;
}

static u16 read_production_info_reg(u16 reg)
{
    if (reg >= PROD_SN_REG_BASE && reg < (PROD_SN_REG_BASE + PROD_SN_REG_COUNT))
    {
        return read_ascii_string_reg(ProductionInfor.BMS_SerialNumber,
                                     PRODUCT_ID_LENGTH_MAX,
                                     reg - PROD_SN_REG_BASE);
    }

    if (reg >= PROD_HW_VER_REG_BASE && reg < (PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT))
    {
        return read_ascii_string_reg(ProductionInfor.BMS_HardWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     reg - PROD_HW_VER_REG_BASE);
    }

    if (reg >= PROD_SW_VER_REG_BASE && reg < (PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT))
    {
        return read_ascii_string_reg(ProductionInfor.BMS_SoftWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     reg - PROD_SW_VER_REG_BASE);
    }

    return 0;
}

#if 1
void WriteProID_Default(void)
{
    UINT8 harewareCount = sizeof(BMS_HARDWARE_VERDION_DEFAULT) > 32 ? 32 : sizeof(BMS_HARDWARE_VERDION_DEFAULT);
    UINT8 softwareCount = sizeof(BMS_SOFTWARE_VERDION_DEFAULT) > 32 ? 32 : sizeof(BMS_SOFTWARE_VERDION_DEFAULT);
    UINT8 serialNumberCount = sizeof(BMS_SERIAL_NUMBER_DEFAULT) > 32 ? 32 : sizeof(BMS_SERIAL_NUMBER_DEFAULT);

    memset(&ProductionInfor, 0, sizeof(PRODUCTION_ID_INFO));

    memcpy(&ProductionInfor.BMS_HardWareVersion[0], BMS_HARDWARE_VERDION_DEFAULT, harewareCount);
    memcpy(&ProductionInfor.BMS_SoftWareVersion[0], BMS_SOFTWARE_VERDION_DEFAULT, softwareCount);
    memcpy(&ProductionInfor.BMS_SerialNumber[0], BMS_SERIAL_NUMBER_DEFAULT, serialNumberCount);
}
#else

void WriteProID_Default(void)
{
    const char *hw = BMS_HARDWARE_VERDION_DEFAULT;
    const char *sw = BMS_SOFTWARE_VERDION_DEFAULT;
    const char *sn = BMS_SERIAL_NUMBER_DEFAULT;

    UINT8 hardwareCount = strlen(hw);
    UINT8 softwareCount = strlen(sw);
    UINT8 serialNumberCount = strlen(sn);

    if (hardwareCount > PRODUCT_ID_LENGTH_MAX - 1)
    {
        hardwareCount = PRODUCT_ID_LENGTH_MAX - 1;
    }
    if (softwareCount > PRODUCT_ID_LENGTH_MAX - 1)
    {
        softwareCount = PRODUCT_ID_LENGTH_MAX - 1;
    }
    if (serialNumberCount > PRODUCT_ID_LENGTH_MAX - 1)
    {
        serialNumberCount = PRODUCT_ID_LENGTH_MAX - 1;
    }

    memset(&ProductionInfor, 0, sizeof(PRODUCTION_ID_INFO));

    memcpy(ProductionInfor.BMS_HardWareVersion, hw, hardwareCount);
    memcpy(ProductionInfor.BMS_SoftWareVersion, sw, softwareCount);
    memcpy(ProductionInfor.BMS_SerialNumber, sn, serialNumberCount);

    ProductionInfor.BMS_HardWareVersion[hardwareCount] = '\0';
    ProductionInfor.BMS_SoftWareVersion[softwareCount] = '\0';
    ProductionInfor.BMS_SerialNumber[serialNumberCount] = '\0';

    ProductionInfor.BMS_HardWareVersionLength = hardwareCount;
    ProductionInfor.BMS_SoftWareVersionLength = softwareCount;
    ProductionInfor.BMS_SerialNumberLength = serialNumberCount;

    ProductionInfor.BMS_SerialNumber_WriteFlag = 0;
    ProductionInfor.BMS_HardWareVersion_WriteFlag = 0;
    ProductionInfor.BMS_SoftWareVersion_WriteFlag = 0;
}
#endif
