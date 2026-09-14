#include "dvc1124.h"
#include "bms_afe.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
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

    /* Do not inherit board-specific charger/switch GPIO assumptions here.
     * Recover current latches only after measured current is below the configured
     * recovery threshold. Keep SCD latched until that backend gets its own
     * hardware-verified load-release policy. */
    if (g_stCellInfoReport.u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv)
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK));

    if (g_stCellInfoReport.u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv)
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK));

    if (clear_mask == 0u) return alarm;

    /* 0x00 is W0C and is intentionally owned by the dedicated command API. */
    if (!DVC1124_ClearAlarmFlags(clear_mask)) return alarm;
    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) return alarm;
    return verify;
}

static void dvc_merge_hw_faults(uint8_t alarm)
{
    bms_fault_reg_t *f = &g_stCellInfoReport.unMdlFault_Third;

    if (alarm & DVC1124_ALARM_COV_MASK) f->bits.b1CellOvp = 1u;
    if (alarm & DVC1124_ALARM_CUV_MASK) f->bits.b1CellUvp = 1u;
    if (alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK))
        f->bits.b1IdischgOcp = 1u;
    if (alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK))
        f->bits.b1IchgOcp = 1u;

    /* SCD remains a hardware/backend lockout. Do not fold its release policy
     * into the generic software-threshold state machine. */
    if (alarm & DVC1124_ALARM_SCD_MASK)
    {
        if (!bms_error_get(BMS_ERROR_CBC_DSG))
            bms_error_raise(BMS_ERROR_CBC_DSG);
    }
    else if (bms_error_get(BMS_ERROR_CBC_DSG))
    {
        bms_error_clear(BMS_ERROR_CBC_DSG);
    }
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
    bms_sw_protection_inputs_t sw;
    uint16_t battery_temp;
    uint16_t mos_temp;
    uint8_t alarm;

    DVC1124_App_AFEGet();
    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return;

    DVC1124_GetConfig(&cfg);
    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm);

    memset(&sw, 0, sizeof(sw));
    battery_temp = dvc_get_configured_temperature(cfg.battery_ntc_gp);
    mos_temp = dvc_get_configured_temperature(cfg.mos_ntc_gp);
    sw.battery_temp_valid = battery_temp ? 1u : 0u;
    sw.mos_temp_valid = mos_temp ? 1u : 0u;
    sw.battery_temp_min = battery_temp;
    sw.battery_temp_max = battery_temp;
    sw.mos_temp = mos_temp;
    bms_sw_protection_update(&sw);
    dvc_merge_hw_faults(alarm);
    bms_sw_protection_record_fault_edges();

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
