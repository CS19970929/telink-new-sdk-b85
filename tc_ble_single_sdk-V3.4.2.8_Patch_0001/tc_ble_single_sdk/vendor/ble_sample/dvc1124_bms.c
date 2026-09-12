#include "dvc1124.h"
#include "dvc1124_commands.h"
#include "bms_afe.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "bms_error.h"
#include "bms_state.h"
#include "param.h"
#include <string.h>

/* bms_afe_sample() is scheduled every 200 ms in the current project. */
#define DVC_BMS_SAMPLE_PERIOD_MS 200u

typedef struct
{
    uint16_t assert_count;
    uint8_t active;
} dvc_bms_filter_t;

typedef enum
{
    DVC_LIMIT_HIGH = 0,
    DVC_LIMIT_LOW
} dvc_limit_direction_t;

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

static uint8_t dvc_filter_update(dvc_bms_filter_t *state,
                                 uint16_t value,
                                 uint16_t trip,
                                 uint16_t recover,
                                 uint16_t filter_10ms,
                                 dvc_limit_direction_t direction)
{
    uint16_t required;
    uint8_t recovered;
    uint8_t violated;

    if (state == NULL) return 0u;
    if (trip == 0u)
    {
        state->active = 0u;
        state->assert_count = 0u;
        return 0u;
    }

    if (state->active)
    {
        recovered = (direction == DVC_LIMIT_HIGH) ?
                    (value <= recover) : (value >= recover);
        if (recovered)
        {
            state->active = 0u;
            state->assert_count = 0u;
        }
        return state->active;
    }

    violated = (direction == DVC_LIMIT_HIGH) ?
               (value >= trip) : (value <= trip);
    if (!violated)
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
    uint8_t verify;

    /* COV/CUV clear only after the configured recovery voltage is reached. */
    if ((alarm & DVC1124_ALARM_COV_MASK) &&
        (g_tParam.protect.u16VcellOvp_Rcv != 0u) &&
        (g_stCellInfoReport.u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv))
    {
        clear_mask |= DVC1124_ALARM_COV_MASK;
    }
    if ((alarm & DVC1124_ALARM_CUV_MASK) &&
        (g_tParam.protect.u16VcellUvp_Rcv != 0u) &&
        (g_stCellInfoReport.u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv))
    {
        clear_mask |= DVC1124_ALARM_CUV_MASK;
    }

    /* Require removal of the source before clearing current/short latches. */
    if (gpio_read(CHG_IN_PIN))
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK));

    if (gpio_read(SW_PIN))
    {
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCD1_MASK |
                                         DVC1124_ALARM_OCD2_MASK |
                                         DVC1124_ALARM_SCD_MASK));
    }

    if (clear_mask == 0u) return alarm;

    /* 0x00 is W0C and is intentionally owned by the dedicated command API. */
    if (!DVC1124_ClearAlarmFlags(clear_mask)) return alarm;
    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) return alarm;
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
            bms_fault_history_record(event); \
    } while (0)

    DVC_RECORD_RISING(b1CellOvp, BMS_FAULT_CELL_OVP_THIRD);
    DVC_RECORD_RISING(b1CellUvp, BMS_FAULT_CELL_UVP_THIRD);
    DVC_RECORD_RISING(b1BatOvp, BMS_FAULT_BAT_OVP_THIRD);
    DVC_RECORD_RISING(b1BatUvp, BMS_FAULT_BAT_UVP_THIRD);
    DVC_RECORD_RISING(b1IchgOcp, BMS_FAULT_CHG_OCP_THIRD);
    DVC_RECORD_RISING(b1IdischgOcp, BMS_FAULT_DSG_OCP_THIRD);
    DVC_RECORD_RISING(b1CellChgOtp, BMS_FAULT_CHG_OTP_THIRD);
    DVC_RECORD_RISING(b1CellChgUtp, BMS_FAULT_CHG_UTP_THIRD);
    DVC_RECORD_RISING(b1CellDischgOtp, BMS_FAULT_DSG_OTP_THIRD);
    DVC_RECORD_RISING(b1CellDischgUtp, BMS_FAULT_DSG_UTP_THIRD);
    DVC_RECORD_RISING(b1TmosOtp, BMS_FAULT_MOS_OTP_THIRD);

#undef DVC_RECORD_RISING
    s_prev_managed_faults = now;
}

static void dvc_publish_faults(uint8_t alarm, const dvc1124_config_t *cfg)
{
    uint16_t battery_temp = 0u;
    uint16_t mos_temp = 0u;
    uint16_t pack_x100 = g_stCellInfoReport.u16VCellTotle;
    union MDLCHGFAULT_REG managed;

    g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp = (alarm & DVC1124_ALARM_COV_MASK) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp = (alarm & DVC1124_ALARM_CUV_MASK) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp =
        (alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK)) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp =
        (alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)) ? 1u : 0u;

    g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp =
        dvc_filter_update(&s_bat_ovp,
                          pack_x100,
                          g_tParam.protect.u16VbusOvp_Third,
                          g_tParam.protect.u16VbusOvp_Rcv,
                          g_tParam.protect.u16VbusOvp_Filter,
                          DVC_LIMIT_HIGH);
    g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp =
        dvc_filter_update(&s_bat_uvp,
                          pack_x100,
                          g_tParam.protect.u16VbusUvp_Third,
                          g_tParam.protect.u16VbusUvp_Rcv,
                          g_tParam.protect.u16VbusUvp_Filter,
                          DVC_LIMIT_LOW);

    if (cfg != NULL)
    {
        battery_temp = dvc_get_configured_temperature(cfg->battery_ntc_gp);
        mos_temp = dvc_get_configured_temperature(cfg->mos_ntc_gp);
    }

    if (battery_temp != 0u)
    {
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp =
            dvc_filter_update(&s_chg_otp, battery_temp,
                              g_tParam.protect.u16TChgOTp_Third,
                              g_tParam.protect.u16TChgOTp_Rcv,
                              g_tParam.protect.u16TChgOTp_Filter,
                              DVC_LIMIT_HIGH);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp =
            dvc_filter_update(&s_chg_utp, battery_temp,
                              g_tParam.protect.u16TchgUTp_Third,
                              g_tParam.protect.u16TchgUTp_Rcv,
                              g_tParam.protect.u16TchgUTp_Filter,
                              DVC_LIMIT_LOW);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp =
            dvc_filter_update(&s_dsg_otp, battery_temp,
                              g_tParam.protect.u16TdischgOTp_Third,
                              g_tParam.protect.u16TdischgOTp_Rcv,
                              g_tParam.protect.u16TdischgOTp_Filter,
                              DVC_LIMIT_HIGH);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp =
            dvc_filter_update(&s_dsg_utp, battery_temp,
                              g_tParam.protect.u16TdischgUTp_Third,
                              g_tParam.protect.u16TdischgUTp_Rcv,
                              g_tParam.protect.u16TdischgUTp_Filter,
                              DVC_LIMIT_LOW);
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
            dvc_filter_update(&s_mos_otp, mos_temp,
                              g_tParam.protect.u16TmosOTp_Third,
                              g_tParam.protect.u16TmosOTp_Rcv,
                              g_tParam.protect.u16TmosOTp_Filter,
                              DVC_LIMIT_HIGH);
    }
    else
    {
        s_mos_otp.active = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp = 0u;
    }

    if (alarm & DVC1124_ALARM_SCD_MASK)
    {
        if (!bms_error_get(BMS_ERROR_CBC_DSG))
            bms_error_raise(BMS_ERROR_CBC_DSG);
    }
    else if (bms_error_get(BMS_ERROR_CBC_DSG))
    {
        bms_error_clear(BMS_ERROR_CBC_DSG);
    }

    managed = dvc_make_managed_fault_snapshot();
    dvc_record_rising_faults(managed);
}

static uint8_t dvc_charge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

static uint8_t dvc_discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

static uint16_t dvc_legacy_adc_mv(uint32_t resistance_ohm)
{
    uint32_t mv;

    if (resistance_ohm == 0u) return 0u;
    mv = (3300u * resistance_ohm) / (resistance_ohm + 10000u);
    return (uint16_t)((mv > 3299u) ? 3299u : mv);
}

static uint32_t dvc_legacy_resistance_100ohm(uint16_t adc_mv)
{
    if (adc_mv >= 3300u) adc_mv = 3299u;
    return (100u * adc_mv) / (3300u - adc_mv);
}

static uint8_t dvc_enforce_fault_fet_state(void)
{
    uint8_t charge_on = g_bms_system_status.bits.b1Status_MOS_CHG ? 1u : 0u;
    uint8_t discharge_on = g_bms_system_status.bits.b1Status_MOS_DSG ? 1u : 0u;
    uint8_t requested_charge = charge_on;
    uint8_t requested_discharge = discharge_on;

    if (dvc_charge_blocked()) charge_on = 0u;
    if (dvc_discharge_blocked()) discharge_on = 0u;

    if ((charge_on == requested_charge) && (discharge_on == requested_discharge))
        return 1u;

    if (!DVC1124_SetMosState(charge_on, discharge_on))
    {
        bms_error_raise(BMS_ERROR_AFE1);
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

uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    if (charge_on && dvc_charge_blocked()) charge_on = 0u;
    if (discharge_on && dvc_discharge_blocked()) discharge_on = 0u;

    if (DVC1124_SetMosState(charge_on, discharge_on)) return 1u;

    bms_error_raise(BMS_ERROR_AFE1);
    return 0u;
}

void bms_afe_set_output_enabled(uint8_t enabled)
{
    DVC1124_SetOutputEnabled(enabled);
}

uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurements)
{
    dvc1124_snapshot_t snapshot;
    dvc1124_config_t cfg;
    uint32_t pack_adc_mv;

    if (measurements == NULL) return 0u;
    memset(measurements, 0, sizeof(*measurements));

    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return 0u;
    DVC1124_GetConfig(&cfg);

    if ((cfg.battery_ntc_gp != 0u) && (cfg.battery_ntc_gp <= DVC1124_MAX_GP))
    {
        measurements->battery_ntc_mv =
            dvc_legacy_adc_mv(snapshot.ntc_res_ohm[cfg.battery_ntc_gp - 1u]);
        measurements->battery_ntc_100ohm =
            dvc_legacy_resistance_100ohm(measurements->battery_ntc_mv);
    }
    if ((cfg.mos_ntc_gp != 0u) && (cfg.mos_ntc_gp <= DVC1124_MAX_GP))
    {
        measurements->mos_ntc_mv =
            dvc_legacy_adc_mv(snapshot.ntc_res_ohm[cfg.mos_ntc_gp - 1u]);
        measurements->mos_ntc_100ohm =
            dvc_legacy_resistance_100ohm(measurements->mos_ntc_mv);
    }

    /* Preserve the existing divider quantization used by the safety monitor. */
    pack_adc_mv = (snapshot.vtop_mv * 15u) / 485u;
    if (pack_adc_mv > 3299u) pack_adc_mv = 3299u;
    measurements->pack_voltage_mv = (pack_adc_mv * 485u) / 15u;
    return 1u;
}
