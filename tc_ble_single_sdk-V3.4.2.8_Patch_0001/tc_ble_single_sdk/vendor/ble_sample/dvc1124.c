#define DVC1124_IMPLEMENTATION 1
#include "dvc1124.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"
#include "param.h"
#include <string.h>

#define DVC_REG_ALARM              0x00u
#define DVC_REG_STATUS             0x01u
#define DVC_REG_CC2_H              0x04u
#define DVC_REG_CC2_M              0x05u
#define DVC_REG_CC2_L_FLAGS        0x06u
#define DVC_REG_VTOP_H             0x07u
#define DVC_REG_PACK_H             0x09u
#define DVC_REG_LOAD_H             0x0Bu
#define DVC_REG_DIE_TEMP_H         0x0Du
#define DVC_REG_V1P8_H             0x0Fu
#define DVC_REG_GP1_H              0x11u
#define DVC_REG_CELL1_H            0x1Du
#define DVC_REG_FET_CTRL           0x51u
#define DVC_REG_CADC_CTRL          0x55u
#define DVC_REG_OCD1_THR           0x59u
#define DVC_REG_OCC1_THR           0x5Au
#define DVC_REG_OCD1_DLY           0x5Bu
#define DVC_REG_OCC1_DLY           0x5Cu
#define DVC_REG_OCD2               0x5Eu
#define DVC_REG_OCC2               0x5Fu
#define DVC_REG_OCD2_DLY           0x60u
#define DVC_REG_OCC2_DLY           0x61u
#define DVC_REG_SCD                0x62u
#define DVC_REG_SCD_DLY            0x63u
#define DVC_REG_BAL_24_17          0x67u
#define DVC_REG_CELL_MASK_24_17    0x6Au
#define DVC_REG_CP_MISC            0x6Du
#define DVC_REG_VADC_CTRL          0x6Eu
#define DVC_REG_COV_H              0x70u
#define DVC_REG_CUV_H              0x72u
#define DVC_REG_GP123_MODE         0x74u
#define DVC_REG_GP456_MODE         0x75u
#define DVC_REG_FRT                0x7Eu
#define DVC_REG_CHIP_VERSION       0x8Fu

#define DVC_ALARM_COV              0x40u
#define DVC_ALARM_CUV              0x20u
#define DVC_ALARM_OCD1             0x10u
#define DVC_ALARM_OCC1             0x08u
#define DVC_ALARM_OCD2             0x04u
#define DVC_ALARM_OCC2             0x02u
#define DVC_ALARM_SCD              0x01u

#define DVC_CST_RESET_REGS         0x0Du
#define DVC_CST_SLEEP              0x0Eu
#define DVC_CST_SHUTDOWN           0x0Fu

#define DVC_CADC_HSFM              0x80u
#define DVC_CADC_CAEW              0x08u
#define DVC_CADC_CAES              0x04u

#define DVC_CPVS_MASK              0xE0u
#define DVC_CPVS_10V               0xA0u
#define DVC_COW                    0x10u
#define DVC_CMM                    0x08u
#define DVC_CVS                    0x04u

#define DVC_VAE                    0x80u
#define DVC_VASM                   0x40u
#define DVC_VAMP_MASK              0x30u
#define DVC_VAO_MASK               0x03u
#define DVC_VAO_1P54MS             0x01u

#define DVC_FET_DSGC_MASK          0x0Cu
#define DVC_FET_CHGC_MASK          0x03u
#define DVC_FET_DSG_ON             0x0Cu
#define DVC_FET_CHG_ON             0x03u

#define DVC_GP123_HSD008           0x49u /* GP1=NTC, GP2=NTC, GP3=NTC */
#define DVC_GP456_HSD008           0x7Fu /* GP4=NTC, GP5=low-side CHG, GP6=low-side DSG */

#define DVC_MEAS_LAST_REG          0x4Cu
#define DVC_MEAS_BYTES             (DVC_MEAS_LAST_REG + 1u)
#define DVC_I2C_RAW_MAX            (2u * (DVC1124_MAX_REGISTER + 1u))
#define DVC_I2C_RETRY_COUNT        3u
#define DVC_READY_RETRY_COUNT      20u
#define DVC_TEMP_TABLE_LEN         56u

extern struct stCell_Info g_stCellInfoReport;
extern UINT32 u32_ChgCur_mA;
extern UINT32 u32_DsgCur_mA;
extern const UINT16 iSheldTemp_10K_AFE[DVC_TEMP_TABLE_LEN];
extern UINT16 GetEndValue(const UINT16 *ptbl, UINT16 tblsize, UINT16 dat);

static dvc1124_config_t s_cfg = {
    DVC1124_DEFAULT_MODEL,
    DVC1124_DEFAULT_ADDR_MODE,
    DVC1124_DEFAULT_HARDWIRE_CODE,
    DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR,
    DVC1124_DEFAULT_CELL_COUNT,
    DVC1124_DEFAULT_SHUNT_UOHM,
    DVC1124_DEFAULT_BATTERY_NTC_GP,
    DVC1124_DEFAULT_MOS_NTC_GP,
};

static dvc1124_snapshot_t s_snapshot;
static uint8_t s_i2c_raw[DVC_I2C_RAW_MAX];
static uint8_t s_bus_initialized;
static uint8_t s_need_config = 1u;
static uint8_t s_legacy_output_gate;
static unsigned int s_compat_adc_pin;
static uint8_t s_compat_adc_virtual;
static uint32_t s_balance_mask;

static uint8_t dvc_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;

    while (len-- != 0u)
    {
        crc ^= *data++;
        for (i = 0u; i < 8u; ++i)
        {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t dvc_is_virtual_pin(unsigned int pin)
{
    return (pin >= DVC1124_VPIN_AFE_CTL) && (pin <= DVC1124_VPIN_NOOP1);
}

static void dvc_delay_ms(uint16_t ms)
{
    uint32_t tick = clock_time();
    uint32_t us = (uint32_t)ms * 1000u;

    while (!clock_time_exceed(tick, us))
    {
        Feed_IWatchDog;
    }
}

static void dvc_bus_init(void)
{
    uint8_t write_addr = DVC1124_FIXED_WRITE_ADDR;

    (void)DVC1124_ResolveWriteAddress(s_cfg.model, s_cfg.addr_mode,
                                      s_cfg.hardwire_code,
                                      s_cfg.explicit_write_addr,
                                      &write_addr);
    i2c_gpio_set(I2C_GPIO_GROUP_C0C1);
    i2c_master_init(write_addr, (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4u * 100000u)));
    s_bus_initialized = 1u;
}

static uint16_t dvc_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int32_t dvc_sign_extend20(uint32_t raw)
{
    raw &= 0x000FFFFFu;
    if ((raw & 0x00080000u) != 0u)
    {
        raw |= 0xFFF00000u;
    }
    return (int32_t)raw;
}

static uint16_t dvc_cell_k_x10000(uint32_t common_mode_mv)
{
    if (common_mode_mv < 24000u) return 10000u;
    if (common_mode_mv < 40000u) return 9999u;
    if (common_mode_mv < 51000u) return 9998u;
    if (common_mode_mv < 60000u) return 9997u;
    if (common_mode_mv < 68000u) return 9996u;
    if (common_mode_mv < 75000u) return 9995u;
    if (common_mode_mv < 82000u) return 9994u;
    if (common_mode_mv < 88000u) return 9993u;
    if (common_mode_mv < 93000u) return 9992u;
    if (common_mode_mv < 99000u) return 9991u;
    if (common_mode_mv < 104000u) return 9990u;
    if (common_mode_mv < 109000u) return 9989u;
    if (common_mode_mv < 113000u) return 9988u;
    if (common_mode_mv < 117000u) return 9987u;
    return 9986u;
}

static uint16_t dvc_correct_cell_mv(uint16_t raw_code, uint32_t common_mode_mv)
{
    uint32_t k = dvc_cell_k_x10000(common_mode_mv);
    uint32_t numerator = (uint32_t)raw_code * 10000u;
    uint32_t denominator = k * 10u;

    return (uint16_t)((numerator + denominator / 2u) / denominator);
}

static uint8_t dvc_write_verified(uint8_t reg, uint8_t value)
{
    uint8_t readback = 0u;

    if (!DVC1124_WriteRegisters(reg, &value, 1u))
    {
        return 0u;
    }
    if (!DVC1124_ReadRegisters(reg, &readback, 1u))
    {
        return 0u;
    }
    return (readback == value) ? 1u : 0u;
}

static uint8_t dvc_update_reg(uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value;

    if (!DVC1124_ReadRegisters(reg, &value, 1u))
    {
        return 0u;
    }
    value = (uint8_t)((value & (uint8_t)~clear_mask) | set_mask);
    return dvc_write_verified(reg, value);
}

static uint8_t dvc_delay_code_voltage(uint32_t delay_ms)
{
    static const uint16_t table_ms[16] = {
        200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
        1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
    };
    uint8_t i;

    if (delay_ms <= table_ms[0]) return 0u;
    for (i = 1u; i < 16u; ++i)
    {
        if (delay_ms <= table_ms[i]) return i;
    }
    return 15u;
}

static uint8_t dvc_delay_code_linear(uint32_t delay_ms, uint16_t step_ms)
{
    uint32_t code;

    if (delay_ms <= step_ms) return 0u;
    code = (delay_ms + step_ms - 1u) / step_ms;
    if (code > 0u) --code;
    if (code > 255u) code = 255u;
    return (uint8_t)code;
}

static uint8_t dvc_current_to_oc1_code(uint16_t current_a_x10)
{
    uint32_t sense_uv;
    uint32_t code;

    if ((current_a_x10 == 0u) || (s_cfg.shunt_uohm == 0u)) return 0u;
    sense_uv = ((uint32_t)current_a_x10 * s_cfg.shunt_uohm) / 10u;
    code = sense_uv / 250u; /* 0.25 mV = 250 uV/bit; round toward safer/lower threshold. */
    if (code == 0u) code = 1u;
    if (code > 255u) code = 255u;
    return (uint8_t)code;
}

static uint8_t dvc_current_to_oc2_reg(uint16_t current_a_x10)
{
    uint32_t sense_uv;
    uint32_t threshold_index;

    if ((current_a_x10 == 0u) || (s_cfg.shunt_uohm == 0u)) return 0u;
    sense_uv = ((uint32_t)current_a_x10 * s_cfg.shunt_uohm) / 10u;
    threshold_index = sense_uv / 4000u;
    if (threshold_index == 0u) threshold_index = 1u; /* DVC minimum is 4 mV. */
    if (threshold_index > 64u) threshold_index = 64u;
    return (uint8_t)(0x80u | (uint8_t)(threshold_index - 1u));
}

static uint8_t dvc_apply_cell_masks(void)
{
    uint8_t mask[3] = {0u, 0u, 0u};
    uint8_t cell;

    for (cell = 5u; cell <= DVC1124_MAX_CELLS; ++cell)
    {
        if (cell <= s_cfg.cell_count) continue;
        if (cell >= 17u)
        {
            mask[0] |= (uint8_t)(1u << (cell - 17u));
        }
        else if (cell >= 9u)
        {
            mask[1] |= (uint8_t)(1u << (cell - 9u));
        }
        else
        {
            mask[2] |= (uint8_t)(1u << (cell - 1u));
        }
    }

    if (!DVC1124_WriteRegisters(DVC_REG_CELL_MASK_24_17, mask, 3u)) return 0u;
    {
        uint8_t verify[3];
        if (!DVC1124_ReadRegisters(DVC_REG_CELL_MASK_24_17, verify, 3u)) return 0u;
        if (memcmp(mask, verify, sizeof(mask)) != 0) return 0u;
    }
    return 1u;
}

static uint8_t dvc_apply_basic_config(void)
{
    uint8_t ok = 1u;

    ok &= dvc_apply_cell_masks();
    ok &= dvc_write_verified(DVC_REG_GP123_MODE, DVC_GP123_HSD008);
    ok &= dvc_write_verified(DVC_REG_GP456_MODE, DVC_GP456_HSD008);

    /* GP5/GP6 are the board's low-side CHG/DSG outputs. Keep HSFM=0; CHG/DSG high-side pins are NC on HS-D008. */
    ok &= dvc_update_reg(DVC_REG_CADC_CTRL,
                         (uint8_t)(DVC_CADC_HSFM | DVC_CADC_CAEW | DVC_CADC_CAES),
                         (uint8_t)(DVC_CADC_CAEW | DVC_CADC_CAES));

    /* 10 V charge-pump setting, normal unsigned cell readings, open-wire trigger left off. */
    ok &= dvc_update_reg(DVC_REG_CP_MISC,
                         (uint8_t)(DVC_CPVS_MASK | DVC_COW | DVC_CMM | DVC_CVS),
                         DVC_CPVS_10V);

    /* VADC synchronized to every CC2 period, 1.54 ms/channel setting. */
    ok &= dvc_update_reg(DVC_REG_VADC_CTRL,
                         (uint8_t)(DVC_VAE | DVC_VASM | DVC_VAMP_MASK | DVC_VAO_MASK),
                         (uint8_t)(DVC_VAE | DVC_VASM | DVC_VAO_1P54MS));

    /* Start with both paths off. The existing mos_update() will request the desired state. */
    ok &= DVC1124_SetMosState(0u, 0u);
    return ok;
}

static uint8_t dvc_apply_protection_from_params(void)
{
    uint16_t cov_mv = g_tParam.protect.u16VcellOvp_Third;
    uint16_t cuv_mv = g_tParam.protect.u16VcellUvp_Third;
    uint32_t cov_delay_ms = (uint32_t)g_tParam.protect.u16VcellOvp_Filter * 10u;
    uint32_t cuv_delay_ms = (uint32_t)g_tParam.protect.u16VcellUvp_Filter * 10u;
    uint32_t oc_delay_ms = (uint32_t)g_tParam.protect.u16IdsgOcp_Filter * 10u;
    uint8_t buf[2];
    uint16_t code12;
    uint8_t ok = 1u;

    if (cov_mv <= 500u)
    {
        code12 = 0u;
    }
    else
    {
        code12 = (uint16_t)(cov_mv - 500u);
        if (code12 > 4095u) code12 = 4095u;
    }
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | dvc_delay_code_voltage(cov_delay_ms));
    ok &= DVC1124_WriteRegisters(DVC_REG_COV_H, buf, 2u);

    code12 = cuv_mv;
    if (code12 > 4095u) code12 = 4095u;
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | dvc_delay_code_voltage(cuv_delay_ms));
    ok &= DVC1124_WriteRegisters(DVC_REG_CUV_H, buf, 2u);

    buf[0] = dvc_current_to_oc1_code(g_tParam.protect.u16IdsgOcp_First);
    ok &= dvc_write_verified(DVC_REG_OCD1_THR, buf[0]);
    buf[0] = dvc_current_to_oc1_code(g_tParam.protect.u16IchgOcp_First);
    ok &= dvc_write_verified(DVC_REG_OCC1_THR, buf[0]);
    ok &= dvc_write_verified(DVC_REG_OCD1_DLY, dvc_delay_code_linear(oc_delay_ms, 8u));
    ok &= dvc_write_verified(DVC_REG_OCC1_DLY, dvc_delay_code_linear((uint32_t)g_tParam.protect.u16IchgOcp_Filter * 10u, 8u));

    ok &= dvc_write_verified(DVC_REG_OCD2, dvc_current_to_oc2_reg(g_tParam.protect.u16IdsgOcp_Second));
    ok &= dvc_write_verified(DVC_REG_OCC2, dvc_current_to_oc2_reg(g_tParam.protect.u16IchgOcp_Second));
    ok &= dvc_write_verified(DVC_REG_OCD2_DLY, dvc_delay_code_linear(oc_delay_ms, 4u));
    ok &= dvc_write_verified(DVC_REG_OCC2_DLY, dvc_delay_code_linear((uint32_t)g_tParam.protect.u16IchgOcp_Filter * 10u, 4u));

    ok &= DVC1124_SetShortCircuitProtection(DVC1124_HW_SCD_THRESHOLD_MV,
                                            DVC1124_HW_SCD_DELAY_US);
    return ok;
}

static void dvc_note_comm_result(uint8_t ok)
{
    if (ok)
    {
        SystemStatus.bits.b1Status_AFE1 = 1u;
        if (System_ERROR_UserCallback(ERROR_STATUS_AFE1))
        {
            (void)System_ERROR_UserCallback(ERROR_REMOVE_AFE1);
        }
    }
    else
    {
        SystemStatus.bits.b1Status_AFE1 = 0u;
        (void)System_ERROR_UserCallback(ERROR_AFE1);
    }
}

static uint16_t dvc_ntc_temp_report(uint32_t r_ohm)
{
    uint32_t code = r_ohm / 10u;

    if (code > 65535u) code = 65535u;
    return GetEndValue(iSheldTemp_10K_AFE, DVC_TEMP_TABLE_LEN, (uint16_t)code);
}

static uint32_t dvc_ntc_resistance(uint16_t gp_code, uint16_t v1p8_code, uint16_t rpu_ohm)
{
    uint32_t denominator;

    if ((gp_code == 0u) || (v1p8_code <= gp_code)) return 0u;
    denominator = (uint32_t)v1p8_code - gp_code;
    return ((uint32_t)gp_code * rpu_ohm) / denominator;
}

static unsigned int dvc_resistance_to_legacy_adc_mv(uint32_t r_ohm)
{
    uint32_t mv;

    if (r_ohm == 0u) return 0u;
    mv = (3300u * r_ohm) / (r_ohm + 10000u);
    if (mv > 3299u) mv = 3299u;
    return (unsigned int)mv;
}

uint8_t DVC1124_ResolveWriteAddress(dvc1124_model_t model,
                                    dvc1124_addr_mode_t mode,
                                    uint8_t hardwire_code,
                                    uint8_t explicit_write_addr,
                                    uint8_t *write_addr)
{
    uint8_t addr;

    if (write_addr == NULL) return 0u;
    if ((model != DVC1124_MODEL_22) && (model != DVC1124_MODEL_24)) return 0u;

    switch (mode)
    {
    case DVC1124_ADDR_FIXED:
        addr = DVC1124_FIXED_WRITE_ADDR;
        break;
    case DVC1124_ADDR_HARDWIRED:
        if (model == DVC1124_MODEL_22)
        {
            if (hardwire_code > 3u) return 0u;
        }
        else if (hardwire_code > 15u)
        {
            return 0u;
        }
        addr = (uint8_t)(DVC1124_HARDWIRE_BASE_WRITE_ADDR | (uint8_t)(hardwire_code << 1));
        break;
    case DVC1124_ADDR_EXPLICIT:
        if ((explicit_write_addr & 0x01u) != 0u) return 0u;
        addr = explicit_write_addr;
        break;
    default:
        return 0u;
    }

    *write_addr = addr;
    return 1u;
}

uint8_t DVC1124_SetAddressConfig(dvc1124_model_t model,
                                 dvc1124_addr_mode_t mode,
                                 uint8_t hardwire_code,
                                 uint8_t explicit_write_addr)
{
    uint8_t addr;

    if (!DVC1124_ResolveWriteAddress(model, mode, hardwire_code,
                                     explicit_write_addr, &addr))
    {
        return 0u;
    }

    s_cfg.model = model;
    s_cfg.addr_mode = mode;
    s_cfg.hardwire_code = hardwire_code;
    s_cfg.explicit_write_addr = explicit_write_addr;
    if (s_bus_initialized)
    {
        i2c_set_id(addr);
    }
    s_snapshot.write_addr = addr;
    s_need_config = 1u;
    return 1u;
}

uint8_t DVC1124_SetCellCount(uint8_t cell_count)
{
    if ((cell_count < 4u) || (cell_count > DVC1124_MAX_CELLS)) return 0u;
    s_cfg.cell_count = cell_count;
    s_need_config = 1u;
    return 1u;
}

uint8_t DVC1124_SetShuntUohm(uint32_t shunt_uohm)
{
    if (shunt_uohm == 0u) return 0u;
    s_cfg.shunt_uohm = shunt_uohm;
    s_need_config = 1u;
    return 1u;
}

void DVC1124_GetConfig(dvc1124_config_t *config)
{
    if (config != NULL) *config = s_cfg;
}

void DVC1124_GetSnapshot(dvc1124_snapshot_t *snapshot)
{
    if (snapshot != NULL) *snapshot = s_snapshot;
}

uint8_t DVC1124_GetWriteAddress(void)
{
    uint8_t addr = DVC1124_FIXED_WRITE_ADDR;
    (void)DVC1124_ResolveWriteAddress(s_cfg.model, s_cfg.addr_mode,
                                      s_cfg.hardwire_code,
                                      s_cfg.explicit_write_addr, &addr);
    return addr;
}

uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t write_addr;
    uint8_t read_addr;
    uint8_t attempt;
    uint8_t i;

    if ((data == NULL) || (len == 0u)) return 0u;
    if (((uint16_t)reg + len - 1u) > DVC1124_MAX_REGISTER) return 0u;
    if (((uint16_t)len * 2u) > sizeof(s_i2c_raw)) return 0u;

    write_addr = DVC1124_GetWriteAddress();
    read_addr = (uint8_t)(write_addr | 0x01u);
    if (!s_bus_initialized) dvc_bus_init();

    for (attempt = 0u; attempt < DVC_I2C_RETRY_COUNT; ++attempt)
    {
        uint8_t first_crc_input[4];
        uint8_t valid = 1u;

        i2c_set_id(write_addr);
        memset(s_i2c_raw, 0, (size_t)len * 2u);
        i2c_read_series(reg, 1u, s_i2c_raw, (int)len * 2);

        first_crc_input[0] = write_addr;
        first_crc_input[1] = reg;
        first_crc_input[2] = read_addr;
        first_crc_input[3] = s_i2c_raw[0];
        if (dvc_crc8(first_crc_input, 4u) != s_i2c_raw[1])
        {
            valid = 0u;
        }

        for (i = 1u; (i < len) && valid; ++i)
        {
            if (dvc_crc8(&s_i2c_raw[(uint16_t)i * 2u], 1u) !=
                s_i2c_raw[(uint16_t)i * 2u + 1u])
            {
                valid = 0u;
            }
        }

        if (valid)
        {
            for (i = 0u; i < len; ++i)
            {
                data[i] = s_i2c_raw[(uint16_t)i * 2u];
            }
            return 1u;
        }

        dvc_delay_ms(1u);
    }

    return 0u;
}

uint8_t DVC1124_WriteRegisters(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t write_addr;
    uint8_t first_crc_input[3];
    uint8_t i;

    if ((data == NULL) || (len == 0u)) return 0u;
    if (((uint16_t)reg + len - 1u) > DVC1124_MAX_REGISTER) return 0u;
    if (((uint16_t)len * 2u) > sizeof(s_i2c_raw)) return 0u;

    write_addr = DVC1124_GetWriteAddress();
    if (!s_bus_initialized) dvc_bus_init();
    i2c_set_id(write_addr);

    first_crc_input[0] = write_addr;
    first_crc_input[1] = reg;
    first_crc_input[2] = data[0];
    s_i2c_raw[0] = data[0];
    s_i2c_raw[1] = dvc_crc8(first_crc_input, 3u);
    for (i = 1u; i < len; ++i)
    {
        s_i2c_raw[(uint16_t)i * 2u] = data[i];
        s_i2c_raw[(uint16_t)i * 2u + 1u] = dvc_crc8(&data[i], 1u);
    }

    i2c_write_series(reg, 1u, s_i2c_raw, (int)len * 2);
    return 1u;
}

uint8_t DVC1124_SetMosState(uint8_t charge_on, uint8_t discharge_on)
{
    uint8_t fet;

    if (!DVC1124_ReadRegisters(DVC_REG_FET_CTRL, &fet, 1u)) return 0u;
    fet &= (uint8_t)~(DVC_FET_DSGC_MASK | DVC_FET_CHGC_MASK);
    if (charge_on && s_legacy_output_gate) fet |= DVC_FET_CHG_ON;
    if (discharge_on && s_legacy_output_gate) fet |= DVC_FET_DSG_ON;
    return dvc_write_verified(DVC_REG_FET_CTRL, fet);
}

uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask)
{
    uint8_t data[3];

    cell_mask &= 0x00FFFFFFu;
    if (s_cfg.cell_count < 24u)
    {
        cell_mask &= ((1uL << s_cfg.cell_count) - 1uL);
    }
    data[0] = (uint8_t)(cell_mask >> 16);
    data[1] = (uint8_t)(cell_mask >> 8);
    data[2] = (uint8_t)cell_mask;
    if (!DVC1124_WriteRegisters(DVC_REG_BAL_24_17, data, 3u)) return 0u;
    s_balance_mask = cell_mask;
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(cell_mask & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((cell_mask >> 16) & 0x00FFu);
    return 1u;
}

uint8_t DVC1124_StartOpenWireCheck(void)
{
    uint8_t reg;

    if (!DVC1124_ReadRegisters(DVC_REG_CP_MISC, &reg, 1u)) return 0u;
    reg |= DVC_COW;
    return DVC1124_WriteRegisters(DVC_REG_CP_MISC, &reg, 1u);
}

uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us)
{
    uint8_t scd;
    uint32_t code;

    if (threshold_mv == 0u)
    {
        scd = 0u;
    }
    else
    {
        code = threshold_mv / 10u;
        if (code == 0u) code = 1u;
        if (code > 63u) code = 63u;
        scd = (uint8_t)(0x80u | (uint8_t)code);
    }
    if (!dvc_write_verified(DVC_REG_SCD, scd)) return 0u;

    code = ((uint32_t)delay_us * 100u) / 781u;
    if (code > 255u) code = 255u;
    return dvc_write_verified(DVC_REG_SCD_DLY, (uint8_t)code);
}

void DVC1124_AFE_Reset(void)
{
    uint8_t cmd = DVC_CST_RESET_REGS;

    /* HS-D008 MCU-AFE-EN is PD7 and is active high in the board schematic. */
    gpio_set_func(GPIO_PD7, AS_GPIO);
    gpio_set_input_en(GPIO_PD7, 0u);
    gpio_set_output_en(GPIO_PD7, 1u);
    gpio_write(GPIO_PD7, 1u);
    dvc_delay_ms(20u);

    dvc_bus_init();
    (void)DVC1124_WriteRegisters(DVC_REG_STATUS, &cmd, 1u);
    dvc_delay_ms(5u);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_need_config = 1u;
}

uint8_t DVC1124_AFE_IsReady(void)
{
    uint8_t attempt;
    uint8_t version = 0u;
    uint8_t frt = 0u;

    if (!s_bus_initialized) dvc_bus_init();
    for (attempt = 0u; attempt < DVC_READY_RETRY_COUNT; ++attempt)
    {
        if (DVC1124_ReadRegisters(DVC_REG_CHIP_VERSION, &version, 1u) &&
            DVC1124_ReadRegisters(DVC_REG_FRT, &frt, 1u))
        {
            s_snapshot.chip_version = version;
            s_snapshot.rpu_ohm = (uint16_t)(6800u + (uint16_t)frt * 25u);
            dvc_note_comm_result(1u);
            return 0u; /* preserve legacy AFE_IsReady(): 0 means ready */
        }
        dvc_delay_ms(5u);
    }

    dvc_note_comm_result(0u);
    return 1u;
}

void DVC1124_UpdataAfeConfig(void)
{
    uint8_t ok;

    (void)DVC1124_SetCellCount((uint8_t)SeriesNum);
    if (DVC1124_AFE_IsReady() != 0u)
    {
        return;
    }

    ok = dvc_apply_basic_config();
    ok &= dvc_apply_protection_from_params();
    if (ok)
    {
        s_need_config = 0u;
        dvc_note_comm_result(1u);
    }
    else
    {
        dvc_note_comm_result(0u);
    }
}

void DVC1124_AFE_Sleep(void)
{
    uint8_t cmd = DVC_CST_SLEEP;

    if (!DVC1124_WriteRegisters(DVC_REG_STATUS, &cmd, 1u))
    {
        dvc_note_comm_result(0u);
    }
}

void DVC1124_App_AFEGet(void)
{
    uint8_t data[DVC_MEAS_BYTES];
    uint16_t v1p8_code;
    uint32_t common_mode_mv = 0u;
    uint32_t total_mv = 0u;
    uint16_t max_mv = 0u;
    uint16_t min_mv = 0xFFFFu;
    uint8_t max_pos = 0u;
    uint8_t min_pos = 0u;
    uint8_t i;
    uint32_t raw20;
    int32_t cc2;
    int32_t current_ma;
    int64_t current_num;
    uint8_t write_addr;

    if (!DVC1124_ReadRegisters(DVC_REG_ALARM, data, DVC_MEAS_BYTES))
    {
        s_snapshot.valid = 0u;
        u32_ChgCur_mA = 0u;
        u32_DsgCur_mA = 0u;
        g_stCellInfoReport.u16Ichg = 0u;
        g_stCellInfoReport.u16IDischg = 0u;
        dvc_note_comm_result(0u);
        return;
    }

    if (s_need_config)
    {
        /* A reset/power-cycle restores register defaults. Re-apply project configuration once. */
        DVC1124_UpdataAfeConfig();
        if (s_need_config) return;
        if (!DVC1124_ReadRegisters(DVC_REG_ALARM, data, DVC_MEAS_BYTES))
        {
            dvc_note_comm_result(0u);
            return;
        }
    }

    write_addr = DVC1124_GetWriteAddress();
    s_snapshot.valid = 1u;
    s_snapshot.alarm = data[DVC_REG_ALARM];
    s_snapshot.status = data[DVC_REG_STATUS];
    s_snapshot.write_addr = write_addr;
    s_snapshot.cell_count = s_cfg.cell_count;
    s_snapshot.vtop_mv = ((uint32_t)dvc_be16(&data[DVC_REG_VTOP_H]) * 128u) / 10u;
    s_snapshot.pack_mv = ((uint32_t)dvc_be16(&data[DVC_REG_PACK_H]) * 128u) / 10u;
    s_snapshot.load_mv = ((uint32_t)dvc_be16(&data[DVC_REG_LOAD_H]) * 128u) / 10u;

    raw20 = ((uint32_t)data[DVC_REG_CC2_H] << 12) |
            ((uint32_t)data[DVC_REG_CC2_M] << 4) |
            ((uint32_t)data[DVC_REG_CC2_L_FLAGS] >> 4);
    cc2 = dvc_sign_extend20(raw20);
    current_num = (int64_t)cc2 * 5000;
    current_ma = (int32_t)(current_num / ((int64_t)16 * s_cfg.shunt_uohm));
    s_snapshot.current_ma = current_ma;

    if (current_ma >= 0)
    {
        u32_DsgCur_mA = (uint32_t)current_ma;
        u32_ChgCur_mA = 0u;
        g_stCellInfoReport.u16IDischg = (uint16_t)((u32_DsgCur_mA / 100u) > 65535u ? 65535u : (u32_DsgCur_mA / 100u));
        g_stCellInfoReport.u16Ichg = 0u;
    }
    else
    {
        uint32_t charge_ma = (uint32_t)(-current_ma);
        u32_ChgCur_mA = charge_ma;
        u32_DsgCur_mA = 0u;
        g_stCellInfoReport.u16Ichg = (uint16_t)((charge_ma / 100u) > 65535u ? 65535u : (charge_ma / 100u));
        g_stCellInfoReport.u16IDischg = 0u;
    }

    for (i = 0u; i < s_cfg.cell_count; ++i)
    {
        uint8_t reg = (uint8_t)(DVC_REG_CELL1_H + (uint8_t)(i * 2u));
        uint16_t raw_cell = dvc_be16(&data[reg]);
        uint16_t mv = dvc_correct_cell_mv(raw_cell, common_mode_mv);
        s_snapshot.cell_mv[i] = mv;
        g_stCellInfoReport.u16VCell[i] = mv;
        total_mv += mv;
        common_mode_mv += mv;
        if (mv > max_mv)
        {
            max_mv = mv;
            max_pos = i;
        }
        if (mv < min_mv)
        {
            min_mv = mv;
            min_pos = i;
        }
    }
    for (; i < DVC1124_MAX_CELLS; ++i)
    {
        s_snapshot.cell_mv[i] = 0u;
    }
    for (i = s_cfg.cell_count; i < 32u; ++i)
    {
        g_stCellInfoReport.u16VCell[i] = 61001u;
    }

    g_stCellInfoReport.u16VCellTotle = (uint16_t)((total_mv / 10u) > 65535u ? 65535u : (total_mv / 10u));
    g_stCellInfoReport.u16VCellMax = max_mv;
    g_stCellInfoReport.u16VCellMin = (min_mv == 0xFFFFu) ? 0u : min_mv;
    g_stCellInfoReport.u16VCellDelta = (uint16_t)(max_mv - g_stCellInfoReport.u16VCellMin);
    g_stCellInfoReport.u16VCellMaxPosition = (uint16_t)max_pos + 1u;
    g_stCellInfoReport.u16VCellMinPosition = (uint16_t)min_pos + 1u;

    v1p8_code = dvc_be16(&data[DVC_REG_V1P8_H]);
    for (i = 0u; i < DVC1124_MAX_GP; ++i)
    {
        uint8_t reg = (uint8_t)(DVC_REG_GP1_H + (uint8_t)(i * 2u));
        uint16_t gp_code = dvc_be16(&data[reg]);
        s_snapshot.gp_code[i] = gp_code;
        s_snapshot.ntc_res_ohm[i] = dvc_ntc_resistance(gp_code, v1p8_code, s_snapshot.rpu_ohm);
        if ((i < 4u) && (s_snapshot.ntc_res_ohm[i] != 0u))
        {
            g_stCellInfoReport.u16Temperature[i] = dvc_ntc_temp_report(s_snapshot.ntc_res_ohm[i]);
        }
        else if (i < 4u)
        {
            g_stCellInfoReport.u16Temperature[i] = 0u;
        }
    }

    {
        int32_t die_x10 = ((int32_t)dvc_be16(&data[DVC_REG_DIE_TEMP_H]) * 24467) / 10000 - 2710;
        int32_t report_temp = die_x10 + 400;
        if (report_temp < 0) report_temp = 0;
        if (report_temp > 65535) report_temp = 65535;
        s_snapshot.die_temp_x10 = (int16_t)die_x10;
        g_stCellInfoReport.u16Temperature[4] = (uint16_t)report_temp;
    }

    {
        uint16_t tmax = 0u;
        uint16_t tmin = 0xFFFFu;
        for (i = 0u; i < 5u; ++i)
        {
            uint16_t t = g_stCellInfoReport.u16Temperature[i];
            if (t == 0u) continue;
            if (t > tmax) tmax = t;
            if (t < tmin) tmin = t;
        }
        g_stCellInfoReport.u16TempMax = tmax;
        g_stCellInfoReport.u16TempMin = (tmin == 0xFFFFu) ? 0u : tmin;
    }

    SystemStatus.bits.b1Status_MOS_CHG = (data[DVC_REG_CC2_L_FLAGS] & 0x01u) ? 1u : 0u;
    SystemStatus.bits.b1Status_MOS_DSG = (data[DVC_REG_CC2_L_FLAGS] & 0x02u) ? 1u : 0u;
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(s_balance_mask & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((s_balance_mask >> 16) & 0x00FFu);
    dvc_note_comm_result(1u);
}

uint8_t DVC1124_CompatMTPWrite(uint8_t wr_addr, uint8_t length, const uint8_t *wr_buf)
{
    if ((wr_buf == NULL) || (length == 0u)) return 0u;

    /* Legacy SH367309 MTP_CONF: bit4=CHGMOS, bit5=DSGMOS. */
    if (wr_addr == 0x40u)
    {
        return DVC1124_SetMosState((wr_buf[0] & 0x10u) ? 1u : 0u,
                                   (wr_buf[0] & 0x20u) ? 1u : 0u);
    }
    return 0u;
}

void DVC1124_CompatAdcBaseInit(unsigned int pin)
{
    s_compat_adc_pin = pin;
    s_compat_adc_virtual = ((pin == DVC1124_VPIN_ADC_BAT) ||
                            (pin == DVC1124_VPIN_ADC_PACK) ||
                            (pin == DVC1124_VPIN_ADC_MOS)) ? 1u : 0u;
    if (!s_compat_adc_virtual)
    {
        adc_base_init((GPIO_PinTypeDef)pin);
    }
}

unsigned int DVC1124_CompatAdcSample(void)
{
    uint8_t gp;

    if (!s_compat_adc_virtual) return adc_sample_and_get_result();
    if (!s_snapshot.valid) return 0u;

    if (s_compat_adc_pin == DVC1124_VPIN_ADC_PACK)
    {
        uint32_t mv = (s_snapshot.vtop_mv * 15u) / 485u;
        return (unsigned int)((mv > 3299u) ? 3299u : mv);
    }
    if (s_compat_adc_pin == DVC1124_VPIN_ADC_BAT)
    {
        gp = s_cfg.battery_ntc_gp;
    }
    else if (s_compat_adc_pin == DVC1124_VPIN_ADC_MOS)
    {
        gp = s_cfg.mos_ntc_gp;
    }
    else
    {
        return 0u;
    }

    if ((gp == 0u) || (gp > DVC1124_MAX_GP)) return 0u;
    return dvc_resistance_to_legacy_adc_mv(s_snapshot.ntc_res_ohm[gp - 1u]);
}

void DVC1124_CompatGpioSetFunc(unsigned int pin, unsigned int func)
{
    if (dvc_is_virtual_pin(pin)) return;
    gpio_set_func((GPIO_PinTypeDef)pin, (GPIO_FuncTypeDef)func);
}

void DVC1124_CompatGpioSetInputEn(unsigned int pin, unsigned int value)
{
    if (dvc_is_virtual_pin(pin)) return;
    gpio_set_input_en((GPIO_PinTypeDef)pin, value);
}

void DVC1124_CompatGpioSetOutputEn(unsigned int pin, unsigned int value)
{
    if (dvc_is_virtual_pin(pin)) return;
    gpio_set_output_en((GPIO_PinTypeDef)pin, value);
}

void DVC1124_CompatGpioWrite(unsigned int pin, unsigned int value)
{
    if (pin == DVC1124_VPIN_AFE_CTL)
    {
        if (value == 0u)
        {
            s_legacy_output_gate = 0u;
            (void)DVC1124_SetMosState(0u, 0u);
        }
        else
        {
            s_legacy_output_gate = 1u;
        }
        return;
    }
    if (dvc_is_virtual_pin(pin)) return;
    gpio_write((GPIO_PinTypeDef)pin, value);
}
