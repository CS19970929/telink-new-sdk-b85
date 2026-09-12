#define DVC1124_IMPLEMENTATION 1
#include "dvc1124.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"
#include "param.h"
#include <string.h>

/*
 * Register addresses/bit definitions come only from dvc1124_reg.h.
 * Keep this file limited to driver behavior, conversion and policy plumbing.
 */
#define DVC_MEAS_BYTES             (DVC1124_REG_CELL24_L + 1u)
#define DVC_I2C_RAW_MAX            (2u * (DVC1124_MAX_REGISTER + 1u))
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
static uint32_t s_balance_requested_mask;

/* Last values actually represented by DVC hardware. Used for diagnostics. */
typedef struct
{
    uint16_t cov_mv;
    uint16_t cov_delay_ms;
    uint16_t cuv_mv;
    uint16_t cuv_delay_ms;
    uint16_t ocd1_a_x10;
    uint16_t ocd1_delay_ms;
    uint16_t occ1_a_x10;
    uint16_t occ1_delay_ms;
    uint16_t ocd2_a_x10;
    uint16_t ocd2_delay_ms;
    uint16_t occ2_a_x10;
    uint16_t occ2_delay_ms;
    uint16_t scd_mv;
    uint16_t scd_delay_us;
    uint32_t quantized_mask;
} dvc_applied_protection_t;

static dvc_applied_protection_t s_applied;

#define DVC_QUANT_COV_DLY   (1uL << 0)
#define DVC_QUANT_CUV_DLY   (1uL << 1)
#define DVC_QUANT_OCD1_THR  (1uL << 2)
#define DVC_QUANT_OCC1_THR  (1uL << 3)
#define DVC_QUANT_OCD1_DLY  (1uL << 4)
#define DVC_QUANT_OCC1_DLY  (1uL << 5)
#define DVC_QUANT_OCD2_THR  (1uL << 6)
#define DVC_QUANT_OCC2_THR  (1uL << 7)
#define DVC_QUANT_OCD2_DLY  (1uL << 8)
#define DVC_QUANT_OCC2_DLY  (1uL << 9)

static uint8_t dvc_crc8(const uint8_t *data, uint16_t len)
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
    i2c_master_init(write_addr,
                    (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4u * 100000u)));
    s_bus_initialized = 1u;
}

static void dvc_bus_recover(void)
{
    reset_i2c_module();
    s_bus_initialized = 0u;
    dvc_bus_init();
}

/* Bound every Telink I2C hardware BUSY wait. */
static uint8_t dvc_i2c_wait_done(void)
{
    uint32_t tick = clock_time();

    while ((reg_i2c_status & FLD_I2C_CMD_BUSY) != 0u)
    {
        Feed_IWatchDog;
        if (clock_time_exceed(tick, DVC1124_I2C_CMD_TIMEOUT_US))
        {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t dvc_i2c_address_ok(void)
{
    return ((reg_i2c_status & FLD_I2C_NAK) == 0u) ? 1u : 0u;
}

static uint8_t dvc_i2c_stop(void)
{
    reg_i2c_ctrl = FLD_I2C_CMD_STOP;
    return dvc_i2c_wait_done();
}

static uint8_t dvc_i2c_write_raw(uint8_t write_addr,
                                 uint8_t reg,
                                 const uint8_t *data,
                                 uint16_t len)
{
    uint16_t i;

    reg_i2c_id = (uint8_t)(write_addr & (uint8_t)~FLD_I2C_WRITE_READ_BIT);
    reg_i2c_adr = reg;
    reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_START);
    if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;

    for (i = 0u; i < len; ++i)
    {
        reg_i2c_di = data[i];
        reg_i2c_ctrl = FLD_I2C_CMD_DI;
        if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;
    }
    return dvc_i2c_stop();
}

static uint8_t dvc_i2c_read_raw(uint8_t write_addr,
                                uint8_t reg,
                                uint8_t *data,
                                uint16_t len)
{
    uint16_t i;

    if (len == 0u) return 0u;

    reg_i2c_id = (uint8_t)(write_addr & (uint8_t)~FLD_I2C_WRITE_READ_BIT);
    reg_i2c_adr = reg;
    reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_START);
    if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;

    reg_i2c_id = (uint8_t)(write_addr | FLD_I2C_WRITE_READ_BIT);
    reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_START);
    if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;

    for (i = 0u; i + 1u < len; ++i)
    {
        reg_i2c_ctrl = (FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID);
        if (!dvc_i2c_wait_done()) return 0u;
        data[i] = reg_i2c_di;
    }

    /* Telink SDK uses FLD_I2C_CMD_ACK on the final byte to terminate the read. */
    reg_i2c_ctrl = (FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID | FLD_I2C_CMD_ACK);
    if (!dvc_i2c_wait_done()) return 0u;
    data[len - 1u] = reg_i2c_di;
    return dvc_i2c_stop();
}

static uint16_t dvc_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int32_t dvc_sign_extend20(uint32_t raw)
{
    raw &= 0x000FFFFFu;
    if ((raw & 0x00080000u) != 0u) raw |= 0xFFF00000u;
    return (int32_t)raw;
}

/* V1.2 page 32 common-mode correction lookup, K * 10000. */
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
    uint32_t denominator = k * 10u; /* raw LSB = 0.1 mV */
    return (uint16_t)((numerator + denominator / 2u) / denominator);
}

static uint8_t dvc_write_verified_block(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t verify[8];
    uint8_t attempt;

    if ((data == NULL) || (len == 0u) || (len > sizeof(verify))) return 0u;

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        if (DVC1124_WriteRegisters(reg, data, len) &&
            DVC1124_ReadRegisters(reg, verify, len) &&
            (memcmp(data, verify, len) == 0))
        {
            return 1u;
        }
        dvc_delay_ms(1u);
    }
    return 0u;
}

static uint8_t dvc_write_verified(uint8_t reg, uint8_t value)
{
    return dvc_write_verified_block(reg, &value, 1u);
}

/* Preserve every bit outside clear_mask. Verify only bits owned by this operation. */
static uint8_t dvc_update_reg(uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value;
    uint8_t target;
    uint8_t readback;
    uint8_t attempt;

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        if (!DVC1124_ReadRegisters(reg, &value, 1u))
        {
            dvc_delay_ms(1u);
            continue;
        }
        target = (uint8_t)((value & (uint8_t)~clear_mask) | (set_mask & clear_mask));
        if (DVC1124_WriteRegisters(reg, &target, 1u) &&
            DVC1124_ReadRegisters(reg, &readback, 1u) &&
            ((readback & clear_mask) == (target & clear_mask)))
        {
            return 1u;
        }
        dvc_delay_ms(1u);
    }
    return 0u;
}

/* Choose the largest supported voltage-protection delay not exceeding request. */
static uint8_t dvc_voltage_delay_code(uint32_t requested_ms, uint16_t *actual_ms)
{
    static const uint16_t table_ms[16] = {
        200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
        1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
    };
    int i;

    if (requested_ms < table_ms[0])
    {
        *actual_ms = table_ms[0];
        return 0u;
    }
    for (i = 15; i >= 0; --i)
    {
        if (requested_ms >= table_ms[i])
        {
            *actual_ms = table_ms[i];
            return (uint8_t)i;
        }
    }
    *actual_ms = table_ms[0];
    return 0u;
}

/* OC1/OC2 delay is (code+1)*step. Floor so protection is not later than request. */
static uint8_t dvc_linear_delay_code(uint32_t requested_ms,
                                     uint16_t step_ms,
                                     uint16_t *actual_ms)
{
    uint32_t steps;

    if (requested_ms < step_ms)
    {
        *actual_ms = step_ms;
        return 0u;
    }
    steps = requested_ms / step_ms;
    if (steps == 0u) steps = 1u;
    if (steps > 256u) steps = 256u;
    *actual_ms = (uint16_t)(steps * step_ms);
    return (uint8_t)(steps - 1u);
}

static uint16_t dvc_current_from_sense_uv(uint32_t sense_uv)
{
    uint32_t value;
    if (s_cfg.shunt_uohm == 0u) return 0u;
    value = (sense_uv * 10u + s_cfg.shunt_uohm / 2u) / s_cfg.shunt_uohm;
    if (value > 65535u) value = 65535u;
    return (uint16_t)value;
}

static uint8_t dvc_current_to_oc1_code(uint16_t requested_a_x10, uint16_t *actual_a_x10)
{
    uint32_t sense_uv;
    uint32_t code;

    if (requested_a_x10 == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }
    if (s_cfg.shunt_uohm == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }

    sense_uv = ((uint32_t)requested_a_x10 * s_cfg.shunt_uohm) / 10u;
    code = sense_uv / 250u; /* threshold = code * 0.25 mV */
    if (code == 0u) code = 1u;
    if (code > 255u) code = 255u;
    *actual_a_x10 = dvc_current_from_sense_uv(code * 250u);
    return (uint8_t)code;
}

static uint8_t dvc_current_to_oc2_code(uint16_t requested_a_x10, uint16_t *actual_a_x10)
{
    uint32_t sense_uv;
    uint32_t index;

    if (requested_a_x10 == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }
    if (s_cfg.shunt_uohm == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }

    sense_uv = ((uint32_t)requested_a_x10 * s_cfg.shunt_uohm) / 10u;
    index = sense_uv / 4000u; /* threshold = (code+1)*4 mV */
    if (index == 0u) index = 1u; /* hardware minimum 4 mV */
    if (index > 64u) index = 64u;
    *actual_a_x10 = dvc_current_from_sense_uv(index * 4000u);
    return (uint8_t)(index - 1u);
}

static void dvc_note_quant(uint32_t bit, uint32_t requested, uint32_t actual)
{
    if (requested != actual) s_applied.quantized_mask |= bit;
}

static uint8_t dvc_apply_cell_masks(void)
{
    uint8_t mask[3] = {0u, 0u, 0u};
    uint8_t cell;

    /* CM[5]..CM[24] mask unused upper channels; DVC1124-2 supports 4..24S. */
    for (cell = 5u; cell <= DVC1124_MAX_CELLS; ++cell)
    {
        if (cell <= s_cfg.cell_count) continue;
        if (cell >= 17u)
            mask[0] |= (uint8_t)(1u << (cell - 17u));
        else if (cell >= 9u)
            mask[1] |= (uint8_t)(1u << (cell - 9u));
        else
            mask[2] |= (uint8_t)(1u << (cell - 1u));
    }
    return dvc_write_verified_block(DVC1124_REG_CELL_MASK_24_17, mask, 3u);
}

static uint8_t dvc_encode_current_wake(uint16_t threshold_uv, uint8_t *code)
{
    if (threshold_uv == 0u) { *code = 0u; return 1u; }
    if ((threshold_uv < 10u) || (threshold_uv > 2550u) || ((threshold_uv % 10u) != 0u)) return 0u;
    *code = (uint8_t)(threshold_uv / 10u);
    return 1u;
}

static uint8_t dvc_encode_body_diode(uint16_t threshold_uv, uint8_t *code)
{
    if (threshold_uv == 0u) { *code = 0u; return 1u; }
    if ((threshold_uv < 40u) || (threshold_uv > 10200u) || ((threshold_uv % 40u) != 0u)) return 0u;
    *code = (uint8_t)(threshold_uv / 40u);
    return 1u;
}

static uint8_t dvc_i2c_watchdog_code(uint8_t seconds, uint8_t *code)
{
    switch (seconds)
    {
    case 0u:  *code = DVC1124_I2C_WDT_OFF; return 1u;
    case 4u:  *code = DVC1124_I2C_WDT_4S; return 1u;
    case 8u:  *code = DVC1124_I2C_WDT_8S; return 1u;
    case 16u: *code = DVC1124_I2C_WDT_16S; return 1u;
    case 32u: *code = DVC1124_I2C_WDT_32S; return 1u;
    default: return 0u;
    }
}

static uint8_t dvc_apply_basic_config(void)
{
    uint8_t ok = 1u;
    uint8_t current_wake_code;
    uint8_t body_diode_code;
    uint8_t watchdog_code;
    uint8_t cpvs_bits;

    if (DVC1124_CHARGE_PUMP_VOLTAGE_CODE > 7u) return 0u;
    if (!dvc_encode_current_wake(DVC1124_CURRENT_WAKE_THRESHOLD_UV, &current_wake_code)) return 0u;
    if (!dvc_encode_body_diode(DVC1124_BODY_DIODE_THRESHOLD_UV, &body_diode_code)) return 0u;
    if (!dvc_i2c_watchdog_code(DVC1124_I2C_WATCHDOG_SECONDS, &watchdog_code)) return 0u;

    ok &= dvc_apply_cell_masks();
    ok &= dvc_write_verified(DVC1124_REG_GP123_MODE, DVC1124_GP123_MODE_VALUE);
    ok &= dvc_write_verified(DVC1124_REG_GP456_MODE, DVC1124_GP456_MODE_VALUE);

    /* HS-D008 uses GP5/GP6 low-side CHG/DSG. Enable CADC in work and sleep. */
    ok &= dvc_update_reg(DVC1124_REG_CADC_CTRL,
                         (uint8_t)(DVC1124_CADC_HSFM_MASK |
                                   DVC1124_CADC_CAEW_MASK |
                                   DVC1124_CADC_CAES_MASK),
                         (uint8_t)(DVC1124_CADC_CAEW_MASK |
                                   DVC1124_CADC_CAES_MASK));

    cpvs_bits = DVC1124_FIELD_PREP(DVC1124_CPVS_MASK,
                                    DVC1124_CPVS_SHIFT,
                                    DVC1124_CHARGE_PUMP_VOLTAGE_CODE);
    ok &= dvc_update_reg(DVC1124_REG_CP_CTRL,
                         (uint8_t)(DVC1124_CPVS_MASK |
                                   DVC1124_COW_MASK |
                                   DVC1124_CMM_MASK |
                                   DVC1124_CVS_MASK),
                         cpvs_bits); /* COW=0, CMM=0, CVS=0 */

    /* VADC enabled, synchronized with every CC2 cycle, 1.54 ms measurement time. */
    ok &= dvc_update_reg(DVC1124_REG_VADC_CTRL,
                         (uint8_t)(DVC1124_VADC_ENABLE_MASK |
                                   DVC1124_VADC_SYNC_MASK |
                                   DVC1124_VADC_PERIOD_MASK |
                                   DVC1124_VADC_TIME_MASK),
                         (uint8_t)(DVC1124_VADC_ENABLE_MASK |
                                   DVC1124_VADC_SYNC_MASK |
                                   DVC1124_VADC_TIME_1P54MS));

    ok &= dvc_write_verified(DVC1124_REG_CURRENT_WAKE, current_wake_code);
    ok &= dvc_write_verified(DVC1124_REG_BODY_DIODE, body_diode_code);
    ok &= dvc_update_reg(DVC1124_REG_I2C_WDT,
                         DVC1124_I2C_WDT_TIME_MASK,
                         watchdog_code);

#if DVC1124_I2C_TIMEOUT_CLOSE_DSG
    ok &= dvc_update_reg(DVC1124_REG_DSG_MASK, DVC1124_DSGMASK_DWM_MASK, 0u);
#else
    ok &= dvc_update_reg(DVC1124_REG_DSG_MASK,
                         DVC1124_DSGMASK_DWM_MASK,
                         DVC1124_DSGMASK_DWM_MASK);
#endif
#if DVC1124_I2C_TIMEOUT_CLOSE_CHG
    ok &= dvc_update_reg(DVC1124_REG_CHG_MASK, DVC1124_CHGMASK_CWM_MASK, 0u);
#else
    ok &= dvc_update_reg(DVC1124_REG_CHG_MASK,
                         DVC1124_CHGMASK_CWM_MASK,
                         DVC1124_CHGMASK_CWM_MASK);
#endif

    /* Start safe; existing mos_update() requests the application state later. */
    ok &= DVC1124_SetMosState(0u, 0u);
    return ok;
}

static uint8_t dvc_apply_protection_from_params(void)
{
    uint16_t cov_mv = g_tParam.protect.u16VcellOvp_Third;
    uint16_t cuv_mv = g_tParam.protect.u16VcellUvp_Third;
    uint32_t cov_req_dly = (uint32_t)g_tParam.protect.u16VcellOvp_Filter * 10u;
    uint32_t cuv_req_dly = (uint32_t)g_tParam.protect.u16VcellUvp_Filter * 10u;
    uint32_t ocd_req_dly = (uint32_t)g_tParam.protect.u16IdsgOcp_Filter * 10u;
    uint32_t occ_req_dly = (uint32_t)g_tParam.protect.u16IchgOcp_Filter * 10u;
    uint8_t buf[2];
    uint8_t code;
    uint16_t code12;
    uint16_t actual;
    uint8_t ok = 1u;

    memset(&s_applied, 0, sizeof(s_applied));

    /* COV: code 0 disables; enabled threshold = code + 500 mV. */
    if (cov_mv == 0u)
    {
        code12 = 0u;
        s_applied.cov_mv = 0u;
    }
    else
    {
        if ((cov_mv < 501u) || (cov_mv > 4595u)) return 0u;
        code12 = (uint16_t)(cov_mv - 500u);
        s_applied.cov_mv = (uint16_t)(code12 + 500u);
    }
    code = dvc_voltage_delay_code(cov_req_dly, &actual);
    s_applied.cov_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_COV_DLY, cov_req_dly, actual);
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | code);
    ok &= dvc_write_verified_block(DVC1124_REG_COV_H, buf, 2u);

    /* CUV: code 0 disables; enabled threshold = code mV. */
    if (cuv_mv == 0u)
    {
        code12 = 0u;
        s_applied.cuv_mv = 0u;
    }
    else
    {
        if (cuv_mv > 4095u) return 0u;
        code12 = cuv_mv;
        s_applied.cuv_mv = code12;
    }
    code = dvc_voltage_delay_code(cuv_req_dly, &actual);
    s_applied.cuv_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_CUV_DLY, cuv_req_dly, actual);
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | code);
    ok &= dvc_write_verified_block(DVC1124_REG_CUV_H, buf, 2u);

    code = dvc_current_to_oc1_code(g_tParam.protect.u16IdsgOcp_First, &actual);
    s_applied.ocd1_a_x10 = actual;
    dvc_note_quant(DVC_QUANT_OCD1_THR, g_tParam.protect.u16IdsgOcp_First, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_THR, code);

    code = dvc_current_to_oc1_code(g_tParam.protect.u16IchgOcp_First, &actual);
    s_applied.occ1_a_x10 = actual;
    dvc_note_quant(DVC_QUANT_OCC1_THR, g_tParam.protect.u16IchgOcp_First, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_THR, code);

    code = dvc_linear_delay_code(ocd_req_dly, 8u, &actual);
    s_applied.ocd1_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCD1_DLY, ocd_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_DLY, code);

    code = dvc_linear_delay_code(occ_req_dly, 8u, &actual);
    s_applied.occ1_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCC1_DLY, occ_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_DLY, code);

    /* OC2 enable is BIT6; preserve 0x5E/0x5F bit7 with masked RMW. */
    if (g_tParam.protect.u16IdsgOcp_Second == 0u)
    {
        s_applied.ocd2_a_x10 = 0u;
        ok &= dvc_update_reg(DVC1124_REG_OCD2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK |
                                       DVC1124_OC2_THRESHOLD_MASK),
                             0u);
    }
    else
    {
        code = dvc_current_to_oc2_code(g_tParam.protect.u16IdsgOcp_Second, &actual);
        s_applied.ocd2_a_x10 = actual;
        dvc_note_quant(DVC_QUANT_OCD2_THR, g_tParam.protect.u16IdsgOcp_Second, actual);
        ok &= dvc_update_reg(DVC1124_REG_OCD2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK |
                                       DVC1124_OC2_THRESHOLD_MASK),
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | code));
    }

    if (g_tParam.protect.u16IchgOcp_Second == 0u)
    {
        s_applied.occ2_a_x10 = 0u;
        ok &= dvc_update_reg(DVC1124_REG_OCC2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK |
                                       DVC1124_OC2_THRESHOLD_MASK),
                             0u);
    }
    else
    {
        code = dvc_current_to_oc2_code(g_tParam.protect.u16IchgOcp_Second, &actual);
        s_applied.occ2_a_x10 = actual;
        dvc_note_quant(DVC_QUANT_OCC2_THR, g_tParam.protect.u16IchgOcp_Second, actual);
        ok &= dvc_update_reg(DVC1124_REG_OCC2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK |
                                       DVC1124_OC2_THRESHOLD_MASK),
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | code));
    }

    code = dvc_linear_delay_code(ocd_req_dly, 4u, &actual);
    s_applied.ocd2_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCD2_DLY, ocd_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD2_DLY, code);

    code = dvc_linear_delay_code(occ_req_dly, 4u, &actual);
    s_applied.occ2_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCC2_DLY, occ_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC2_DLY, code);

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
            (void)System_ERROR_UserCallback(ERROR_REMOVE_AFE1);
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

static uint8_t dvc_ntc_resistance(uint16_t gp_code,
                                  uint16_t v1p8_code,
                                  uint16_t rpu_ohm,
                                  uint32_t *res_ohm)
{
    uint32_t denominator;

    if (res_ohm == NULL) return 0u;
    *res_ohm = 0u;
    if (v1p8_code == 0u) return 0u;
    if (gp_code == 0u) return 0u;             /* short / invalid input */
    if (v1p8_code <= gp_code) return 0u;      /* open / saturation */

    denominator = (uint32_t)v1p8_code - gp_code;
    *res_ohm = ((uint32_t)gp_code * rpu_ohm) / denominator;
    return 1u;
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
                                     explicit_write_addr, &addr)) return 0u;

    s_cfg.model = model;
    s_cfg.addr_mode = mode;
    s_cfg.hardwire_code = hardwire_code;
    s_cfg.explicit_write_addr = explicit_write_addr;
    if (s_bus_initialized) i2c_set_id(addr);
    s_snapshot.write_addr = addr;
    s_need_config = 1u;
    return 1u;
}

uint8_t DVC1124_SetCellCount(uint8_t cell_count)
{
    /* DVC1124-2 V1.2 explicitly supports 4..24 cells. */
    if ((cell_count < DVC1124_MIN_CELLS) || (cell_count > DVC1124_MAX_CELLS)) return 0u;
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

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        uint8_t first_crc_input[4];
        uint8_t valid = 1u;

        memset(s_i2c_raw, 0, (size_t)len * 2u);
        if (!dvc_i2c_read_raw(write_addr, reg, s_i2c_raw, (uint16_t)len * 2u))
        {
            dvc_bus_recover();
            dvc_delay_ms(1u);
            continue;
        }

        first_crc_input[0] = write_addr;
        first_crc_input[1] = reg;
        first_crc_input[2] = read_addr;
        first_crc_input[3] = s_i2c_raw[0];
        if (dvc_crc8(first_crc_input, 4u) != s_i2c_raw[1]) valid = 0u;

        for (i = 1u; (i < len) && valid; ++i)
        {
            if (dvc_crc8(&s_i2c_raw[(uint16_t)i * 2u], 1u) !=
                s_i2c_raw[(uint16_t)i * 2u + 1u]) valid = 0u;
        }

        if (valid)
        {
            for (i = 0u; i < len; ++i) data[i] = s_i2c_raw[(uint16_t)i * 2u];
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
    uint8_t attempt;
    uint8_t i;

    if ((data == NULL) || (len == 0u)) return 0u;
    if (((uint16_t)reg + len - 1u) > DVC1124_MAX_REGISTER) return 0u;
    if (((uint16_t)len * 2u) > sizeof(s_i2c_raw)) return 0u;

    write_addr = DVC1124_GetWriteAddress();
    if (!s_bus_initialized) dvc_bus_init();

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

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        if (dvc_i2c_write_raw(write_addr, reg, s_i2c_raw, (uint16_t)len * 2u)) return 1u;
        dvc_bus_recover();
        dvc_delay_ms(1u);
    }
    return 0u;
}

uint8_t DVC1124_SetMosState(uint8_t charge_on, uint8_t discharge_on)
{
    uint8_t set = 0u;

    if (charge_on && s_legacy_output_gate)
    {
        set |= DVC1124_FIELD_PREP(DVC1124_FET_CHGC_MASK,
                                   DVC1124_FET_CHGC_SHIFT,
                                   DVC1124_FET_DRIVE_ON);
    }
    if (discharge_on && s_legacy_output_gate)
    {
        set |= DVC1124_FIELD_PREP(DVC1124_FET_DSGC_MASK,
                                   DVC1124_FET_DSGC_SHIFT,
                                   DVC1124_FET_DRIVE_ON);
    }

    return dvc_update_reg(DVC1124_REG_FET_CTRL,
                          (uint8_t)(DVC1124_FET_DSGC_MASK |
                                    DVC1124_FET_CHGC_MASK),
                          set);
}

static uint8_t dvc_refresh_balance_state(void)
{
    uint8_t data[3];
    uint32_t actual;

    if (!DVC1124_ReadRegisters(DVC1124_REG_BAL_24_17, data, 3u)) return 0u;
    actual = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (s_cfg.cell_count < 24u) actual &= ((1uL << s_cfg.cell_count) - 1uL);
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(actual & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((actual >> 16) & 0x00FFu);
    return 1u;
}

uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask)
{
    uint8_t data[3];

    cell_mask &= 0x00FFFFFFu;
    if (s_cfg.cell_count < 24u) cell_mask &= ((1uL << s_cfg.cell_count) - 1uL);

    data[0] = (uint8_t)(cell_mask >> 16);
    data[1] = (uint8_t)(cell_mask >> 8);
    data[2] = (uint8_t)cell_mask;
    if (!dvc_write_verified_block(DVC1124_REG_BAL_24_17, data, 3u)) return 0u;

    s_balance_requested_mask = cell_mask;
    (void)s_balance_requested_mask; /* request must be refreshed by upper layer before 60 s timeout */
    return dvc_refresh_balance_state();
}

uint8_t DVC1124_StartOpenWireCheck(void)
{
    uint8_t reg;

    /* COW is self-clearing after about 1 s, so do not require persistent readback=1. */
    if (!DVC1124_ReadRegisters(DVC1124_REG_CP_CTRL, &reg, 1u)) return 0u;
    reg |= DVC1124_COW_MASK;
    return DVC1124_WriteRegisters(DVC1124_REG_CP_CTRL, &reg, 1u);
}

uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us)
{
    uint8_t threshold_code;
    uint8_t delay_code;
    uint32_t delay_code32;
    uint16_t actual_delay_us;
    uint8_t ok;

    if (threshold_mv == 0u)
    {
        s_applied.scd_mv = 0u;
        s_applied.scd_delay_us = 0u;
        ok = dvc_update_reg(DVC1124_REG_SCD,
                            (uint8_t)(DVC1124_SCD_ENABLE_MASK |
                                      DVC1124_SCD_THRESHOLD_MASK),
                            0u);
        ok &= dvc_write_verified(DVC1124_REG_SCD_DLY, 0u);
        return ok;
    }

    /* V1.2: threshold = SCDT*10 mV. Code 0 is not a useful enabled threshold. */
    if ((threshold_mv < 10u) || (threshold_mv > 630u) || ((threshold_mv % 10u) != 0u)) return 0u;
    threshold_code = (uint8_t)(threshold_mv / 10u);

    /* V1.2: delay = SCDD*7.81 us. Program delay first, then enable SCD. */
    delay_code32 = ((uint32_t)delay_us * 100u) / 781u;
    if (delay_code32 > 255u) return 0u;
    delay_code = (uint8_t)delay_code32;
    actual_delay_us = (uint16_t)(((uint32_t)delay_code * 781u + 50u) / 100u);

    if (!dvc_write_verified(DVC1124_REG_SCD_DLY, delay_code)) return 0u;
    if (!dvc_update_reg(DVC1124_REG_SCD,
                        (uint8_t)(DVC1124_SCD_ENABLE_MASK |
                                  DVC1124_SCD_THRESHOLD_MASK),
                        (uint8_t)(DVC1124_SCD_ENABLE_MASK | threshold_code))) return 0u;

    s_applied.scd_mv = (uint16_t)threshold_code * 10u;
    s_applied.scd_delay_us = actual_delay_us;
    return 1u;
}

void DVC1124_AFE_Reset(void)
{
    uint8_t cmd = (uint8_t)DVC1124_CST_RESET_REGISTERS;

    /* HS-D008 MCU-AFE-EN = PD7, active high. */
    gpio_set_func(GPIO_PD7, AS_GPIO);
    gpio_set_input_en(GPIO_PD7, 0u);
    gpio_set_output_en(GPIO_PD7, 1u);
    gpio_write(GPIO_PD7, 1u);
    dvc_delay_ms(20u);

    dvc_bus_init();
    (void)DVC1124_WriteRegisters(DVC1124_REG_STATUS, &cmd, 1u); /* CST=1101 */
    dvc_delay_ms(DVC1124_RESET_SETTLE_MS);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_applied, 0, sizeof(s_applied));
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
        if (DVC1124_ReadRegisters(DVC1124_REG_CHIP_VERSION, &version, 1u) &&
            DVC1124_ReadRegisters(DVC1124_REG_FRT, &frt, 1u))
        {
            s_snapshot.chip_version = version;
            s_snapshot.rpu_ohm = (uint16_t)(6800u + (uint16_t)frt * 25u);
            dvc_note_comm_result(1u);
            return 0u; /* legacy API: 0 means ready */
        }
        dvc_delay_ms(5u);
    }
    dvc_note_comm_result(0u);
    return 1u;
}

void DVC1124_UpdataAfeConfig(void)
{
    uint8_t ok;

    if (!DVC1124_SetCellCount((uint8_t)SeriesNum))
    {
        dvc_note_comm_result(0u);
        return;
    }
    if (DVC1124_AFE_IsReady() != 0u) return;

    ok = dvc_apply_basic_config();
    ok &= dvc_apply_protection_from_params();
    if (ok)
    {
        s_need_config = 0u;
        dvc_note_comm_result(1u);
    }
    else
    {
        s_need_config = 1u;
        dvc_note_comm_result(0u);
    }
}

void DVC1124_AFE_Sleep(void)
{
    uint8_t cmd = (uint8_t)DVC1124_CST_ENTER_SLEEP;
    if (!DVC1124_WriteRegisters(DVC1124_REG_STATUS, &cmd, 1u)) dvc_note_comm_result(0u);
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
    uint8_t configured_ntc_ok = 1u;

    if (s_need_config)
    {
        DVC1124_UpdataAfeConfig();
        if (s_need_config)
        {
            s_snapshot.valid = 0u;
            return;
        }
    }

    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, data, DVC_MEAS_BYTES))
    {
        s_snapshot.valid = 0u;
        u32_ChgCur_mA = 0u;
        u32_DsgCur_mA = 0u;
        g_stCellInfoReport.u16Ichg = 0u;
        g_stCellInfoReport.u16IDischg = 0u;
        dvc_note_comm_result(0u);
        return;
    }

    write_addr = DVC1124_GetWriteAddress();
    s_snapshot.valid = 1u;
    s_snapshot.alarm = data[DVC1124_REG_ALARM];
    s_snapshot.status = data[DVC1124_REG_STATUS];
    s_snapshot.write_addr = write_addr;
    s_snapshot.cell_count = s_cfg.cell_count;
    s_snapshot.vtop_mv = ((uint32_t)dvc_be16(&data[DVC1124_REG_VTOP_H]) * 128u + 5u) / 10u;
    s_snapshot.pack_mv = ((uint32_t)dvc_be16(&data[DVC1124_REG_VPACK_H]) * 128u + 5u) / 10u;
    s_snapshot.load_mv = ((uint32_t)dvc_be16(&data[DVC1124_REG_VLOAD_H]) * 128u + 5u) / 10u;

    raw20 = ((uint32_t)data[DVC1124_REG_CC2_H] << 12) |
            ((uint32_t)data[DVC1124_REG_CC2_M] << 4) |
            ((uint32_t)data[DVC1124_REG_CC2_L_FLAGS] >> 4);
    cc2 = dvc_sign_extend20(raw20);
    current_num = (int64_t)cc2 * 5000; /* 0.3125 uV = 5000/16 nV */
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
        uint8_t reg = (uint8_t)(DVC1124_REG_CELL1_H + (uint8_t)(i * 2u));
        uint16_t raw_cell = dvc_be16(&data[reg]);
        uint16_t mv = dvc_correct_cell_mv(raw_cell, common_mode_mv);

        s_snapshot.cell_mv[i] = mv;
        g_stCellInfoReport.u16VCell[i] = mv;
        total_mv += mv;
        common_mode_mv += mv;
        if (mv > max_mv) { max_mv = mv; max_pos = i; }
        if (mv < min_mv) { min_mv = mv; min_pos = i; }
    }
    for (; i < DVC1124_MAX_CELLS; ++i) s_snapshot.cell_mv[i] = 0u;
    for (i = s_cfg.cell_count; i < 32u; ++i) g_stCellInfoReport.u16VCell[i] = 61001u;

    g_stCellInfoReport.u16VCellTotle = (uint16_t)((total_mv / 10u) > 65535u ? 65535u : (total_mv / 10u));
    g_stCellInfoReport.u16VCellMax = max_mv;
    g_stCellInfoReport.u16VCellMin = (min_mv == 0xFFFFu) ? 0u : min_mv;
    g_stCellInfoReport.u16VCellDelta = (uint16_t)(max_mv - g_stCellInfoReport.u16VCellMin);
    g_stCellInfoReport.u16VCellMaxPosition = (uint16_t)max_pos + 1u;
    g_stCellInfoReport.u16VCellMinPosition = (uint16_t)min_pos + 1u;

    v1p8_code = dvc_be16(&data[DVC1124_REG_V1P8_H]);
    for (i = 0u; i < DVC1124_MAX_GP; ++i)
    {
        uint8_t reg = (uint8_t)(DVC1124_REG_GP1_H + (uint8_t)(i * 2u));
        uint16_t gp_code = dvc_be16(&data[reg]);
        uint32_t r_ohm = 0u;
        uint8_t ntc_ok;

        s_snapshot.gp_code[i] = gp_code;
        ntc_ok = dvc_ntc_resistance(gp_code, v1p8_code, s_snapshot.rpu_ohm, &r_ohm);
        s_snapshot.ntc_res_ohm[i] = r_ohm;

        if (i < 4u)
            g_stCellInfoReport.u16Temperature[i] = ntc_ok ? dvc_ntc_temp_report(r_ohm) : 0u;

        if (((i + 1u) == s_cfg.battery_ntc_gp || (i + 1u) == s_cfg.mos_ntc_gp) && !ntc_ok)
            configured_ntc_ok = 0u;
    }

    if (configured_ntc_ok)
    {
        if (System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK))
            (void)System_ERROR_UserCallback(ERROR_REMOVE_TEMP_BREAK);
    }
    else
    {
        (void)System_ERROR_UserCallback(ERROR_TEMP_BREAK);
    }

    {
        /* V1.2: T = VCT*0.24467 - 271.03 C; integer unit = 0.1 C. */
        int32_t die_x10 = ((int32_t)dvc_be16(&data[DVC1124_REG_VCT_H]) * 24467) / 10000 - 2710;
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

    SystemStatus.bits.b1Status_MOS_CHG =
        (data[DVC1124_REG_CC2_L_FLAGS] & DVC1124_CC2_CHGF_MASK) ? 1u : 0u;
    SystemStatus.bits.b1Status_MOS_DSG =
        (data[DVC1124_REG_CC2_L_FLAGS] & DVC1124_CC2_DSGF_MASK) ? 1u : 0u;

    /* 0x67..0x69 auto-clear after 60 s; report actual AFE state, not cached request. */
    if (!dvc_refresh_balance_state())
    {
        g_stCellInfoReport.u16BalanceFlag1 = 0u;
        g_stCellInfoReport.u16BalanceFlag2 = 0u;
    }
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
    if (!s_compat_adc_virtual) adc_base_init((GPIO_PinTypeDef)pin);
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
        gp = s_cfg.battery_ntc_gp;
    else if (s_compat_adc_pin == DVC1124_VPIN_ADC_MOS)
        gp = s_cfg.mos_ntc_gp;
    else
        return 0u;

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
