#include "bms_diag.h"
#include "dvc1124.h"
#include "bms_afe.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_afe_hw_profile.h"
#include "param.h"
#include <string.h>

/* bms_afe_sample() is scheduled every 200 ms in the current project. */
#define DVC_BMS_SAMPLE_PERIOD_MS 200u

/* D008 recovery owner: survives AFE reinitialization, not MCU reset.
 * PB1 is meaningful only with a valid DSGF=0 sample. 200 ms high confirmation
 * rejects a single sample/glitch; elapsed SDK 32k time is wrap-safe. */
#define DVC_OCC_RECOVERY_TICKS (30u * 32000u)
#define DVC_LOAD_REMOVED_TICKS (200u * 32u)
#define DVC_OCC_ALARMS (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)
#define DVC_DSG_ALARMS (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK | DVC1124_ALARM_SCD_MASK)
static struct {
    uint32_t charge_started;
    uint32_t removed_started;
    uint32_t last_sample;
    uint8_t sample_seen;
    uint8_t charge;
    uint8_t discharge;
    uint8_t removed_pending;
    uint8_t hw_pending;
} s_current_recovery;

static uint16_t dvc_get_configured_temperature(uint8_t gp)
{
    if ((gp == 0u) || (gp > 4u)) return 0u;
    return g_stCellInfoReport.u16Temperature[gp - 1u];
}

static uint8_t dvc_configured_ntc_valid(const dvc1124_snapshot_t *snapshot,
                                        uint8_t gp)
{
    uint8_t index;

    /* Temperature conversion/reporting is currently implemented for GP1..GP4.
     * Do not infer validity from the encoded temperature value itself: 0 is a
     * valid engineering value for -40.0 C in the legacy (degC + 40) * 10
     * encoding. Use the NTC measurement validity represented by resistance. */
    if ((snapshot == 0) || (gp == 0u) || (gp > 4u)) return 0u;
    index = (uint8_t)(gp - 1u);
    return (snapshot->ntc_res_ohm[index] != 0u) ? 1u : 0u;
}

static uint8_t dvc_get_battery_temperature_range(const dvc1124_snapshot_t *snapshot,
                                                  uint16_t *min_temp,
                                                  uint16_t *max_temp)
{
    uint16_t t1;
    uint16_t t2;

    if ((snapshot == 0) || (min_temp == 0) || (max_temp == 0)) return 0u;
    if (!dvc_configured_ntc_valid(snapshot, DVC1124_DEFAULT_BATTERY_NTC_GP) ||
        !dvc_configured_ntc_valid(snapshot, DVC1124_DEFAULT_BATTERY_NTC2_GP))
    {
        *min_temp = 0u;
        *max_temp = 0u;
        return 0u;
    }

    t1 = dvc_get_configured_temperature(DVC1124_DEFAULT_BATTERY_NTC_GP);
    t2 = dvc_get_configured_temperature(DVC1124_DEFAULT_BATTERY_NTC2_GP);
    *min_temp = (t1 <= t2) ? t1 : t2;
    *max_temp = (t1 >= t2) ? t1 : t2;
    return 1u;
}

static void dvc_publish_temperature_report(const bms_sw_protection_inputs_t *sw)
{
    if (sw == 0) return;

    /*
     * D008 temperature has one owner: DVC1124 GP measurement/conversion.
     * GP2/GP3 are battery temperatures; GP4 is power-MOS temperature.
     * Keep legacy report slots populated from those same engineering values so
     * existing Modbus/upper-computer readers do not need a second NTC lookup.
     * ENV_TEMP3 is the battery maximum-temperature mirror and MOS_TEMP1 is GP4.
     */
    g_stCellInfoReport.u16Temperature[ENV_TEMP3] =
        sw->battery_temp_valid ? sw->battery_temp_max : 0u;
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] =
        sw->mos_temp_valid ? sw->mos_temp : 0u;

    if (sw->battery_temp_valid)
    {
        g_stCellInfoReport.u16TempMin = sw->battery_temp_min;
        g_stCellInfoReport.u16TempMax = sw->battery_temp_max;
    }
    else
    {
        g_stCellInfoReport.u16TempMin = 0u;
        g_stCellInfoReport.u16TempMax = 0u;
    }
}

#if DVC1124_HW_PROTECT_ENABLE
static uint8_t dvc_recovery_stable(uint8_t condition, uint16_t stable_ms, uint16_t *count)
{
    uint16_t required;
    if (count == 0) return 0u;
    if (!condition) { *count = 0u; return 0u; }
    required = (uint16_t)(((uint32_t)stable_ms + DVC_BMS_SAMPLE_PERIOD_MS - 1u) /
                          DVC_BMS_SAMPLE_PERIOD_MS);
    if (required == 0u) required = 1u;
    if (*count < required) ++(*count);
    return (*count >= required) ? 1u : 0u;
}

static uint8_t dvc_clear_recovered_hw_latches(uint8_t alarm)
{
    static uint16_t cov_count;
    static uint16_t cuv_count;
    bms_afe_hw_profile_t hw;
    uint8_t clear_mask = 0u;
    uint8_t verify;

    if (!bms_afe_hw_profile_get(&hw)) return alarm;

    if (alarm & DVC1124_ALARM_COV_MASK) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16VCellMax <= hw.cov_recover_mv),
                                hw.cov_recover_ms, &cov_count))
            clear_mask |= DVC1124_ALARM_COV_MASK;
    } else cov_count = 0u;

    if (alarm & DVC1124_ALARM_CUV_MASK) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16VCellMin >= hw.cuv_recover_mv),
                                hw.cuv_recover_ms, &cuv_count))
            clear_mask |= DVC1124_ALARM_CUV_MASK;
    } else cuv_count = 0u;

    if (clear_mask == 0u) return alarm;
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
#endif

/* Called after the software state machine publishes its own Third bits and
 * before HW faults are merged. Never feed last cycle's merged bits back in. */
static uint8_t dvc_recover_current_faults(const dvc1124_snapshot_t *snapshot,
                                         uint8_t alarm, uint8_t load_removed)
{
    uint32_t now = snapshot->sample_tick_32k;
    uint8_t sw_charge = g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp;
    uint8_t sw_discharge = g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp;
    uint8_t charge_ready = 0u, discharge_ready = 0u;
    uint8_t release_reason = 0u;
    uint8_t before = (uint8_t)(s_current_recovery.charge | (s_current_recovery.discharge << 1));
#if DVC1124_HW_PROTECT_ENABLE
    uint8_t clear_mask, verify;
    s_current_recovery.hw_pending |= (uint8_t)(alarm & (DVC_OCC_ALARMS | DVC_DSG_ALARMS));
#endif
    /* Do not count an unobserved acquisition/reinit gap as continuous proof. */
    if (s_current_recovery.sample_seen &&
        (uint32_t)(now - s_current_recovery.last_sample) > 2u * DVC_BMS_SAMPLE_PERIOD_MS * 32u)
        s_current_recovery.removed_pending = 0u;
    s_current_recovery.sample_seen = 1u;
    s_current_recovery.last_sample = now;
    if (!s_current_recovery.charge && (sw_charge || (alarm & DVC_OCC_ALARMS))) {
        s_current_recovery.charge = 1u;
        s_current_recovery.charge_started = now;
    }
    if (sw_discharge || (alarm & DVC_DSG_ALARMS))
        s_current_recovery.discharge = 1u;

    /* A still-active software threshold remains an independent inhibit. */
    if (s_current_recovery.charge && !sw_charge &&
        (uint32_t)(now - s_current_recovery.charge_started) >= DVC_OCC_RECOVERY_TICKS)
        charge_ready = 1u;
    /* Negative current is charge; the existing +/-200 mA unreliable zone is
     * not evidence of charging. AUTO_DIODE permits reverse charging while the
     * discharge fault remains latched. PB1 alone is ignored while DSGF=1. */
    if (snapshot->current_ma < -(int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA)
        release_reason = 2u;
    else if (load_removed && !(snapshot->status & DVC1124_CC2_DSGF_MASK))
        release_reason = 1u;
    if (s_current_recovery.discharge && release_reason) {
        if (s_current_recovery.removed_pending != release_reason) {
            s_current_recovery.removed_pending = release_reason;
            s_current_recovery.removed_started = now;
        } else if (!sw_discharge &&
                   (uint32_t)(now - s_current_recovery.removed_started) >= DVC_LOAD_REMOVED_TICKS) {
            discharge_ready = 1u;
        }
    } else s_current_recovery.removed_pending = 0u;

#if DVC1124_HW_PROTECT_ENABLE
    clear_mask = (uint8_t)(s_current_recovery.hw_pending &
                 ((charge_ready ? DVC_OCC_ALARMS : 0u) |
                  (discharge_ready ? DVC_DSG_ALARMS : 0u)));
    if (clear_mask) {
        /* Keep the software lock until BOTH W1C and readback succeed, including
         * after AFE reinit has already cleared its volatile alarm register. */
        if (!DVC1124_ClearAlarmFlags(clear_mask) ||
            !DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) {
            charge_ready = discharge_ready = 0u;
        } else {
            alarm = verify;
            s_current_recovery.hw_pending &= (uint8_t)~(clear_mask & (uint8_t)~verify);
            if (verify & DVC_OCC_ALARMS) charge_ready = 0u;
            if (verify & DVC_DSG_ALARMS) discharge_ready = 0u;
            /* A different current fault appearing during readback is retained. */
            s_current_recovery.hw_pending |= (uint8_t)(verify & (DVC_OCC_ALARMS | DVC_DSG_ALARMS));
            if (verify & DVC_DSG_ALARMS) s_current_recovery.discharge = 1u;
            if ((verify & DVC_OCC_ALARMS) && !s_current_recovery.charge) {
                s_current_recovery.charge = 1u;
                s_current_recovery.charge_started = now;
            }
        }
    }
#endif
    if (charge_ready) s_current_recovery.charge = 0u;
    if (discharge_ready) {
        s_current_recovery.discharge = 0u;
        s_current_recovery.removed_pending = 0u;
    }
    if (before != (uint8_t)(s_current_recovery.charge | (s_current_recovery.discharge << 1)))
        bms_diag_trace(DIAG_EV_CURRENT_RECOVERY,
            (uint32_t)(s_current_recovery.charge | (s_current_recovery.discharge << 1)),
            (uint32_t)alarm | ((uint32_t)load_removed << 8) | ((uint32_t)snapshot->status << 16) | ((uint32_t)release_reason << 24));
    if (s_current_recovery.charge) g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp = 1u;
    if (s_current_recovery.discharge) g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp = 1u;
    return (uint8_t)(alarm | s_current_recovery.hw_pending);
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

/*
 * R81 CHGC/DSGC are mode commands, while CHGF/DSGF report the resulting DVC
 * driver outputs. A protected AUTO_DIODE command may therefore remain 10b
 * while the DVC autonomously re-opens the physical driver after reverse
 * current exceeds BDPT. Do not re-submit the same R81 mode every 200 ms: a
 * repeated write can disturb that autonomous state even though the requested
 * BMS state has not changed.
 */
static uint8_t dvc_set_fet_modes_if_changed(dvc1124_fet_drive_t charge_mode,
                                            dvc1124_fet_drive_t discharge_mode)
{
    uint8_t current;
    uint8_t target;
    uint8_t ok;

    if (!DVC1124_ReadRegisters(DVC1124_REG_FET_CTRL, &current, 1u)) {
        bms_diag_command(0u, 0u); return 0u;
    }
    bms_diag_command(current, 1u);

    if ((DVC1124_FIELD_GET(DVC1124_FET_CHGC_MASK,
                           DVC1124_FET_CHGC_SHIFT,
                           current) == (uint8_t)charge_mode) &&
        (DVC1124_FIELD_GET(DVC1124_FET_DSGC_MASK,
                           DVC1124_FET_DSGC_SHIFT,
                           current) == (uint8_t)discharge_mode))
    {
        return 1u;
    }

    target = (uint8_t)(current &
                       (uint8_t)~(DVC1124_FET_CHGC_MASK |
                                  DVC1124_FET_DSGC_MASK));
    target |= DVC1124_FIELD_PREP(DVC1124_FET_CHGC_MASK,
                                  DVC1124_FET_CHGC_SHIFT,
                                  charge_mode);
    target |= DVC1124_FIELD_PREP(DVC1124_FET_DSGC_MASK,
                                  DVC1124_FET_DSGC_SHIFT,
                                  discharge_mode);

    /* One combined R81 write only: never transition through hard-OFF before
     * AUTO_DIODE. DVC1124_WriteRegisterSafe() preserves unrelated fields and
     * verifies the documented writable bits. */
    ok = DVC1124_WriteRegisterSafe(DVC1124_REG_FET_CTRL, target);
    bms_diag_command(target, ok);
    return ok;
}

static uint8_t dvc_apply_common_port_fet_state(uint8_t charge_on,
                                              uint8_t discharge_on)
{
    uint8_t charge_blocked = dvc_charge_blocked();
    uint8_t discharge_blocked = dvc_discharge_blocked();
    dvc1124_fet_drive_t charge_mode = DVC1124_FET_DRIVE_OFF;
    dvc1124_fet_drive_t discharge_mode = DVC1124_FET_DRIVE_OFF;

    /* Normal D008 operation requests both FETs ON. If exactly one direction
     * is protected, command the blocked FET directly to AUTO_DIODE and keep
     * the opposite FET ON. The DVC then handles reverse-current reopening in
     * hardware; the MCU does not poll current direction or repeatedly toggle
     * the protected FET. */
    if (charge_on && discharge_on)
    {
        if (charge_blocked && !discharge_blocked)
        {
            charge_mode = DVC1124_FET_DRIVE_AUTO_DIODE;
            discharge_mode = DVC1124_FET_DRIVE_ON;
        }
        else if (discharge_blocked && !charge_blocked)
        {
            charge_mode = DVC1124_FET_DRIVE_ON;
            discharge_mode = DVC1124_FET_DRIVE_AUTO_DIODE;
        }
        else if (!charge_blocked && !discharge_blocked)
        {
            charge_mode = DVC1124_FET_DRIVE_ON;
            discharge_mode = DVC1124_FET_DRIVE_ON;
        }
        /* Both directions blocked remains a true two-FET hard shutdown. */
    }
    else
    {
        /* Explicit OFF / AFE communication inhibit / sleep / open-wire paths
         * remain hard OFF and must never be upgraded to AUTO_DIODE. */
        if (charge_on && !charge_blocked)
            charge_mode = DVC1124_FET_DRIVE_ON;
        if (discharge_on && !discharge_blocked)
            discharge_mode = DVC1124_FET_DRIVE_ON;
    }

    return dvc_set_fet_modes_if_changed(charge_mode, discharge_mode);
}

void DVC1124_BmsApp_AFEGet(void)
{
    dvc1124_snapshot_t snapshot;
    bms_sw_protection_inputs_t sw;
    uint8_t alarm = 0u;

    uint16_t diag_c = 0u, diag_d = 0u;

    DVC1124_App_AFEGet();
    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) {
        s_current_recovery.removed_pending = 0u;
        bms_diag_driver(0u, 0u); return;
    }

    memset(&sw, 0, sizeof(sw));
    sw.battery_temp_valid = dvc_get_battery_temperature_range(
        &snapshot, &sw.battery_temp_min, &sw.battery_temp_max);
    sw.mos_temp_valid = dvc_configured_ntc_valid(
        &snapshot, DVC1124_DEFAULT_MOS_NTC_GP);
    sw.mos_temp = sw.mos_temp_valid ?
        dvc_get_configured_temperature(DVC1124_DEFAULT_MOS_NTC_GP) : 0u;

    /* Measurement/reporting remains active in every protection-isolation mode. */
    dvc_publish_temperature_report(&sw);

#if DVC1124_SW_PROTECT_ENABLE
    bms_sw_protection_update(&sw);
    if (bms_sw_protection_charge_blocked()) diag_c |= DIAG_BLOCK_SW;
    if (bms_sw_protection_discharge_blocked()) diag_d |= DIAG_BLOCK_SW;
#else
    /* Match D011/D013 isolation semantics: disabling the SW path also clears
     * any previously latched software-managed fault bits and TEMP_BREAK. */
    bms_sw_protection_clear();
#endif

#if DVC1124_HW_PROTECT_ENABLE
    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm);
#endif
    alarm = dvc_recover_current_faults(&snapshot, alarm,
                                      (uint8_t)(gpio_read(CHG_IN_PIN) != 0u));
#if DVC1124_HW_PROTECT_ENABLE
    dvc_merge_hw_faults(alarm);
    if (alarm & (DVC1124_ALARM_COV_MASK | DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)) diag_c |= DIAG_BLOCK_HW;
    if (alarm & (DVC1124_ALARM_CUV_MASK | DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK | DVC1124_ALARM_SCD_MASK)) diag_d |= DIAG_BLOCK_HW;
#else
    /* SCD/COV/CUV/OC flags are not protection inputs in HW-off bench mode.
     * The low-level driver also programs their DVC hardware enables OFF. */
    bms_error_clear(BMS_ERROR_CBC_DSG);
#endif

    if (bms_error_get(BMS_ERROR_TEMP_BREAK)) { diag_c |= DIAG_BLOCK_TEMP; diag_d |= DIAG_BLOCK_TEMP; }
    if (dvc_charge_blocked() && !diag_c) diag_c |= DIAG_BLOCK_BACKEND;
    if (dvc_discharge_blocked() && !diag_d) diag_d |= DIAG_BLOCK_BACKEND;
    bms_diag_backend(diag_c, diag_d);
    bms_sw_protection_record_fault_edges();
    DVC1124_BalanceService((uint8_t)((g_stCellInfoReport.u16Ichg > 0u) &&
                                     !dvc_charge_blocked() &&
                                     !dvc_discharge_blocked()));

    /* FET arbitration is applied by bms_afe_guard immediately after
     * this sample. Keeping it there preserves communication/open-wire hard
     * inhibit semantics while this function only updates fault state. */
}

uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    if (dvc_apply_common_port_fet_state(charge_on, discharge_on)) return 1u;

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
    uint32_t pack_adc_mv;

    if (measurements == NULL) return 0u;
    memset(measurements, 0, sizeof(*measurements));

    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return 0u;

    /* Legacy diagnostic representation only. Battery uses primary GP2;
     * protection uses both GP2/GP3. MOS diagnostic is the real power-MOS GP4. */
    measurements->battery_ntc_mv =
        dvc_legacy_adc_mv(snapshot.ntc_res_ohm[DVC1124_DEFAULT_BATTERY_NTC_GP - 1u]);
    measurements->battery_ntc_100ohm =
        dvc_legacy_resistance_100ohm(measurements->battery_ntc_mv);
    measurements->mos_ntc_mv =
        dvc_legacy_adc_mv(snapshot.ntc_res_ohm[DVC1124_DEFAULT_MOS_NTC_GP - 1u]);
    measurements->mos_ntc_100ohm =
        dvc_legacy_resistance_100ohm(measurements->mos_ntc_mv);

    /* Legacy diagnostic representation only; no MCU ADC is used here. */
    pack_adc_mv = (snapshot.vtop_mv * 15u) / 485u;
    if (pack_adc_mv > 3299u) pack_adc_mv = 3299u;
    measurements->pack_voltage_mv = (pack_adc_mv * 485u) / 15u;
    measurements->current_ma = snapshot.current_ma;
    measurements->sample_tick_32k = snapshot.sample_tick_32k;
    return 1u;
}
