#include "dvc1124.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"
#include "param.h"
#include <string.h>

/* DVC1124 alarm register 0x00. */
#define DVC_BMS_REG_ALARM        0x00u
#define DVC_BMS_ALARM_COV        0x40u
#define DVC_BMS_ALARM_CUV        0x20u
#define DVC_BMS_ALARM_OCD1       0x10u
#define DVC_BMS_ALARM_OCC1       0x08u
#define DVC_BMS_ALARM_OCD2       0x04u
#define DVC_BMS_ALARM_OCC2       0x02u
#define DVC_BMS_ALARM_SCD        0x01u

/* App_AFEGet is scheduled every 200 ms in the current project. */
#define DVC_BMS_SAMPLE_PERIOD_MS 200u

extern struct stCell_Info g_stCellInfoReport;
extern void FaultWarnRecord2(enum FaultFlag num);

typedef struct
{
    uint16_t assert_count;
    uint8_t active;
} dvc_bms_filter_t;

static dvc_bms_filter_t s_bat_ovp;
static dvc_bms_filter_t s_bat_uvp;
static dvc_bms_filter_t s_chg_otp;
static dvc_bms_filter_t s_chg_utp;
static dvc_bms_filter_t s_dsg_otp;
static dvc_bms_filter_t s_dsg_utp;
static dvc_bms_filter_t s_mos_otp;
static union MDLCHGFAULT_REG s_prev_managed_faults;

static uint16_t dvc_filter_samples(uint16_t filter_10ms)
{
    uint32_t delay_ms = (uint32_t)filter_10ms * 10u;
    uint32_t samples;

    if (delay_ms == 0u) return 1u;
    samples = (delay_ms + DVC_BMS_SAMPLE_PERIOD_MS - 1u) / DVC_BMS_SAMPLE_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

static uint8_t dvc_filter_high(dvc_bms_filter_t *state,
                               uint16_t value,
                               uint16_t trip,
                               uint16_t recover,
                               uint16_t filter_10ms)
{
    uint16_t required;

    if (state == NULL) return 0u;
    if (trip == 0u)
    {
        state->active = 0u;
        state->assert_count = 0u;
        return 0u;
    }

    if (state->active)
    {
        if (value <= recover)
        {
            state->active = 0u;
            state->assert_count = 0u;
        }
        return state->active;
    }

    if (value < trip)
    {
        state->assert_count = 0u;
        return 0u;
    }

    required = dvc_filter_samples(filter_10ms);
    if (state->assert_count < required) ++state->assert_count;
    if (state->assert_count >= required)
    {
        state->active = 1u;
        state->assert_count = 0u;
    }
    return state->active;
}

static uint8_t dvc_filter_low(dvc_bms_filter_t *state,
                              uint16_t value,
                              uint16_t trip,
                              uint16_t recover,
                              uint16_t filter_10ms)
{
    uint16_t required;

    if (state == NULL) return 0u;
    if (trip == 0u)
    {
        state->active = 0u;
        state->assert_count = 0u;
        return 0u;
    }

    if (state->active)
    {
        if (value >= recover)
        {
            state->active = 0u;
            state->assert_count = 0u;
        }
        return state->active;
    }

    if (value > trip)
    {
        state->assert_count = 0u;
        return 0u;
    }

    required = dvc_filter_samples(filter_10ms);
    if (state->assert_count < required) ++state->assert_count;
    if (state->assert_count >= required)
    {
        state->active = 1u;
        state->assert_count = 0u;
    }
    return state->active;
}

static uint16_t dvc_get_configured_temperature(uint8_t gp)
{
    if ((gp == 0u) || (gp > 4u)) return 0u;
    return g_stCellInfoReport.u16Temperature[gp - 1u];
}

static uint8_t dvc_clear_recovered_hw_latches(uint8_t alarm)
{
    uint8_t clear_mask = 0u;
    uint8_t write_value;
    uint8_t verify;

    /* COV/CUV clear only after the configured recovery voltage is reached. */
    if ((alarm & DVC_BMS_ALARM_COV) &&
        (g_tParam.protect.u16VcellOvp_Rcv != 0u) &&
        (g_stCellInfoReport.u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv))
    {
        clear_mask |= DVC_BMS_ALARM_COV;
    }
    if ((alarm & DVC_BMS_ALARM_CUV) &&
        (g_tParam.protect.u16VcellUvp_Rcv != 0u) &&
        (g_stCellInfoReport.u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv))
    {
        clear_mask |= DVC_BMS_ALARM_CUV;
    }

    /* Require removal of the source before clearing current/short latches. */
    if (gpio_read(CHG_IN_PIN))
        clear_mask |= (uint8_t)(alarm & (DVC_BMS_ALARM_OCC1 | DVC_BMS_ALARM_OCC2));

    if (gpio_read(SW_PIN))
    {
        clear_mask |= (uint8_t)(alarm & (DVC_BMS_ALARM_OCD1 |
                                         DVC_BMS_ALARM_OCD2 |
                                         DVC_BMS_ALARM_SCD));
    }

    if (clear_mask == 0u) return alarm;

    /*
     * Alarm flags are W0C: writing 0 clears, writing 1 is ineffective.
     * Write 1 to every non-target flag instead of mirroring a stale alarm
     * snapshot; otherwise a new fault that rises between read and write could
     * be unintentionally cleared.
     */
    write_value = (uint8_t)~clear_mask;
    if (!DVC1124_WriteRegisters(DVC_BMS_REG_ALARM, &write_value, 1u)) return alarm;
    if (!DVC1124_ReadRegisters(DVC_BMS_REG_ALARM, &verify, 1u)) return alarm;
    return verify;
}

static union MDLCHGFAULT_REG dvc_make_managed_fault_snapshot(void)
{
    union MDLCHGFAULT_REG managed;

    managed.all = 0u;
    managed.bits.b1CellOvp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp;
    managed.bits.b1CellUvp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp;
    managed.bits.b1BatOvp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp;
    managed.bits.b1BatUvp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp;
    managed.bits.b1IchgOcp = g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp;
    managed.bits.b1IdischgOcp = g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp;
    managed.bits.b1CellChgOtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp;
    managed.bits.b1CellChgUtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp;
    managed.bits.b1CellDischgOtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp;
    managed.bits.b1CellDischgUtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp;
    managed.bits.b1TmosOtp = g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp;
    return managed;
}

static void dvc_record_rising_faults(union MDLCHGFAULT_REG now)
{
#define DVC_RECORD_RISING(field, event) \
    do { \
        if (now.bits.field && !s_prev_managed_faults.bits.field) \
            FaultWarnRecord2(event); \
    } while (0)

    DVC_RECORD_RISING(b1CellOvp, CellOvp_Third);
    DVC_RECORD_RISING(b1CellUvp, CellUvp_Third);
    DVC_RECORD_RISING(b1BatOvp, BatOvp_Third);
    DVC_RECORD_RISING(b1BatUvp, BatUvp_Third);
    DVC_RECORD_RISING(b1IchgOcp, IchgOcp_Third);
    DVC_RECORD_RISING(b1IdischgOcp, IdischgOcp_Third);
    DVC_RECORD_RISING(b1CellChgOtp, CellChgOTp_Third);
    DVC_RECORD_RISING(b1CellChgUtp, CellChgUTp_Third);
    DVC_RECORD_RISING(b1CellDischgOtp, CellDsgOTp_Third);
    DVC_RECORD_RISING(b1CellDischgUtp, CellDsgUTp_Third);
    DVC_RECORD_RISING(b1TmosOtp, MosOTp_Third);

#undef DVC_RECORD_RISING
    s_prev_managed_faults = now;
}

static void dvc_publish_faults(uint8_t alarm, const dvc1124_config_t *cfg)
{
    uint16_t battery_temp = 0u;
    uint16_t mos_temp = 0u;
    uint16_t pack_x100 = g_stCellInfoReport.u16VCellTotle;
    union MDLCHGFAULT_REG managed;

    g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp = (alarm & DVC_BMS_ALARM_COV) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp = (alarm & DVC_BMS_ALARM_CUV) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp =
        (alarm & (DVC_BMS_ALARM_OCD1 | DVC_BMS_ALARM_OCD2)) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp =
        (alarm & (DVC_BMS_ALARM_OCC1 | DVC_BMS_ALARM_OCC2)) ? 1u : 0u;

    g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp =
        dvc_filter_high(&s_bat_ovp,
                        pack_x100,
                        g_tParam.protect.u16VbusOvp_Third,
                        g_tParam.protect.u16VbusOvp_Rcv,
                        g_tParam.protect.u16VbusOvp_Filter);
    g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp =
        dvc_filter_low(&s_bat_uvp,
                       pack_x100,
                       g_tParam.protect.u16VbusUvp_Third,
                       g_tParam.protect.u16VbusUvp_Rcv,
                       g_tParam.protect.u16VbusUvp_Filter);

    if (cfg != NULL)
    {
        battery_temp = dvc_get_configured_temperature(cfg->battery_ntc_gp);
        mos_temp = dvc_get_configured_temperature(cfg->mos_ntc_gp);
    }

    if (battery_temp != 0u)
    {
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp =
            dvc_filter_high(&s_chg_otp, battery_temp,
                            g_tParam.protect.u16TChgOTp_Third,
                            g_tParam.protect.u16TChgOTp_Rcv,
                            g_tParam.protect.u16TChgOTp_Filter);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp =
            dvc_filter_low(&s_chg_utp, battery_temp,
                           g_tParam.protect.u16TchgUTp_Third,
                           g_tParam.protect.u16TchgUTp_Rcv,
                           g_tParam.protect.u16TchgUTp_Filter);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp =
            dvc_filter_high(&s_dsg_otp, battery_temp,
                            g_tParam.protect.u16TdischgOTp_Third,
                            g_tParam.protect.u16TdischgOTp_Rcv,
                            g_tParam.protect.u16TdischgOTp_Filter);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp =
            dvc_filter_low(&s_dsg_utp, battery_temp,
                           g_tParam.protect.u16TdischgUTp_Third,
                           g_tParam.protect.u16TdischgUTp_Rcv,
                           g_tParam.protect.u16TdischgUTp_Filter);
    }
    else
    {
        s_chg_otp.active = s_chg_utp.active = 0u;
        s_dsg_otp.active = s_dsg_utp.active = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp = 0u;
    }

    if (mos_temp != 0u)
    {
        g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp =
            dvc_filter_high(&s_mos_otp, mos_temp,
                            g_tParam.protect.u16TmosOTp_Third,
                            g_tParam.protect.u16TmosOTp_Rcv,
                            g_tParam.protect.u16TmosOTp_Filter);
    }
    else
    {
        s_mos_otp.active = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp = 0u;
    }

    if (alarm & DVC_BMS_ALARM_SCD)
    {
        if (!System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG))
            (void)System_ERROR_UserCallback(ERROR_CBC_DSG);
    }
    else if (System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG))
    {
        (void)System_ERROR_UserCallback(ERROR_REMOVE_CBC_DSG);
    }

    managed = dvc_make_managed_fault_snapshot();
    dvc_record_rising_faults(managed);
}

static uint8_t dvc_charge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK)) ? 1u : 0u;
}

static uint8_t dvc_discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG) ||
            System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK)) ? 1u : 0u;
}

static uint8_t dvc_enforce_fault_fet_state(void)
{
    uint8_t charge_on = SystemStatus.bits.b1Status_MOS_CHG ? 1u : 0u;
    uint8_t discharge_on = SystemStatus.bits.b1Status_MOS_DSG ? 1u : 0u;
    uint8_t requested_charge = charge_on;
    uint8_t requested_discharge = discharge_on;

    if (dvc_charge_blocked()) charge_on = 0u;
    if (dvc_discharge_blocked()) discharge_on = 0u;

    if ((charge_on == requested_charge) && (discharge_on == requested_discharge))
        return 1u;

    if (!DVC1124_SetMosState(charge_on, discharge_on))
    {
        (void)System_ERROR_UserCallback(ERROR_AFE1);
        return 0u;
    }
    return 1u;
}

void DVC1124_BmsApp_AFEGet(void)
{
    dvc1124_snapshot_t snapshot;
    dvc1124_config_t cfg;
    uint8_t alarm;

    DVC1124_App_AFEGet();
    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return;

    DVC1124_GetConfig(&cfg);
    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm);
    dvc_publish_faults(alarm, &cfg);

    /*
     * Hardware COV/CUV/OC/SCD can close DVC outputs autonomously, but pack
     * voltage and external-NTC protections are software-only. Enforce the
     * fault decision immediately; do not wait for app.c mos_update() to notice
     * a target-state change because its target is based on charger/key state.
     */
    (void)dvc_enforce_fault_fet_state();
}

uint8_t DVC1124_BmsCompatMTPWrite(uint8_t wr_addr, uint8_t length, const uint8_t *wr_buf)
{
    uint8_t value;

    if ((wr_buf == NULL) || (length == 0u)) return 0u;

    if (wr_addr != 0x40u)
        return DVC1124_CompatMTPWrite(wr_addr, length, wr_buf);

    value = wr_buf[0];
    /* Legacy SH367309 MTP_CONF: bit4=CHGMOS, bit5=DSGMOS. */
    if ((value & 0x10u) && dvc_charge_blocked()) value &= (uint8_t)~0x10u;
    if ((value & 0x20u) && dvc_discharge_blocked()) value &= (uint8_t)~0x20u;
    return DVC1124_CompatMTPWrite(wr_addr, 1u, &value);
}
