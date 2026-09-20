#include "bms_afe.h"
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "param.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "bms_afe_hw_profile.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include <string.h>

#define SH3510_SAMPLE_MS              200u
#define SH3510_VALID_SNAPSHOT_RELEASE_COUNT 3u
#define SH3510_SHORT_RELEASE_SAMPLES    10u /* 2 s stable LOADOFF at 200 ms */
#define SH3510_OCD_RELEASE_FILTER_10MS  200u /* 2 s stable load-off/charge recovery */

typedef enum {
    HW_REC_OV = 0, HW_REC_UV, HW_REC_OCD1, HW_REC_OCD2,
    HW_REC_OCC, HW_REC_OTC, HW_REC_OTD, HW_REC_UTC, HW_REC_UTD,
    HW_REC_COUNT
} sh3510_hw_recovery_id_t;

static bms_afe_aux_measurements_t s_aux;
static uint32_t s_ntc_ohm[4];
static uint8_t s_ntc_valid[4];
static uint8_t s_output_enabled;
static uint8_t s_snapshot_valid;
static uint8_t s_hw_afe_error;
static uint16_t s_balance_mask;
static uint8_t s_requested_charge_on;
static uint8_t s_requested_discharge_on;
static uint8_t s_output_inhibit;
static uint8_t s_valid_snapshot_streak;
static uint8_t s_short_latched;
static uint8_t s_short_clear_pending;
static uint16_t s_short_release_count;
static uint16_t s_hw_recovery_count[HW_REC_COUNT];
static uint8_t s_hw_charge_protect;
static uint8_t s_hw_discharge_protect;
static uint8_t s_afe_reconfigure_required;
static uint8_t s_bstatus2;
static uint8_t s_fet_command_valid;
static uint8_t s_last_charge_command;
static uint8_t s_last_discharge_command;

/* Existing product 10K NTC table: R in 100 ohm, T=(degC+40)*10. */
static const uint16_t s_ntc_table[] = {
    2037u,0u, 1526u,50u, 1161u,100u, 893u,150u, 694u,200u,
    544u,250u, 430u,300u, 342u,350u, 275u,400u, 221u,450u,
    180u,500u, 147u,550u, 121u,600u, 100u,650u, 83u,700u,
    69u,750u, 58u,800u, 49u,850u, 41u,900u, 35u,950u,
    30u,1000u, 26u,1050u, 22u,1100u, 19u,1150u, 16u,1200u,
    14u,1250u, 12u,1300u, 11u,1350u, 9u,1400u, 8u,1450u
};

#if SH3673510_HW_PROTECT_ENABLE
static uint16_t filter_samples(uint16_t filter_10ms)
{
    uint32_t ms = (uint32_t)filter_10ms * 10u;
    uint32_t n;
    if (ms == 0u) return 1u;
    n = (ms + SH3510_SAMPLE_MS - 1u) / SH3510_SAMPLE_MS;
    if (n == 0u) n = 1u;
    if (n > 65535u) n = 65535u;
    return (uint16_t)n;
}

#endif

static uint16_t ntc_temp(uint32_t ohm)
{
    uint32_t r100 = (ohm + 50u) / 100u;
    if (r100 > 65535u) r100 = 65535u;
    return bms_lookup_u16(s_ntc_table,
                          (uint16_t)(sizeof(s_ntc_table) / sizeof(s_ntc_table[0])),
                          (uint16_t)r100);
}

static uint16_t legacy_adc_mv(uint32_t ohm)
{
    uint32_t mv;
    if (ohm == 0u) return 0u;
    mv = (3300u * ohm) / (ohm + 10000u);
    return (uint16_t)((mv > 3299u) ? 3299u : mv);
}

static void note_comm_error(void)
{
    sh3673520_comm_stats_t stats;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    /* Recovery requires consecutive valid samples, never evidence spanning a
     * communication gap. Preserve fault latches, discard only qualification. */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_fet_command_valid = 0u;
    SH3673520_GetCommStats(&stats);
    if (stats.last_error == SH3673520_ERR_SPI ||
        stats.last_error == SH3673520_ERR_TIMEOUT ||
        stats.last_error == SH3673520_ERR_CRC ||
        stats.last_error == SH3673520_ERR_PROTOCOL) {
        if (!bms_error_get(BMS_ERROR_SPI)) bms_error_raise(BMS_ERROR_SPI);
    }
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    g_bms_system_status.bits.b1Status_AFE1 = 0u;
}

static void note_comm_ok(void)
{
    bms_error_clear(BMS_ERROR_SPI);
    if (!s_hw_afe_error) bms_error_clear(BMS_ERROR_AFE1);
    g_bms_system_status.bits.b1Status_AFE1 = s_hw_afe_error ? 0u : 1u;
}

static uint8_t battery_temperature_snapshot(uint16_t *bat_min,
                                            uint16_t *bat_max)
{
    uint16_t t1;
    uint16_t t2;
    if (bat_min == 0 || bat_max == 0) return 0u;
    if (!s_ntc_valid[SH3673510_D011_BAT_NTC1_INDEX] ||
        !s_ntc_valid[SH3673510_D011_BAT_NTC2_INDEX]) return 0u;
    t1 = g_stCellInfoReport.u16Temperature[AFE1_TEMP1];
    t2 = g_stCellInfoReport.u16Temperature[AFE1_TEMP2];
    *bat_min = (t1 < t2) ? t1 : t2;
    *bat_max = (t1 > t2) ? t1 : t2;
    return 1u;
}

static uint8_t charge_blocked(void)
{
    return (s_hw_charge_protect ||
            bms_sw_protection_charge_blocked() ||
            bms_features_charge_direction_blocked()) ? 1u : 0u;
}

static uint8_t discharge_blocked(void)
{
    return (s_hw_discharge_protect ||
            bms_sw_protection_discharge_blocked() ||
            s_short_latched ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG)) ? 1u : 0u;
}

static uint8_t sh3510_outputs_healthy(void)
{
    return (s_snapshot_valid && !s_output_inhibit && !s_hw_afe_error &&
            !bms_error_get(BMS_ERROR_AFE1) && !bms_error_get(BMS_ERROR_SPI)) ? 1u : 0u;
}

static uint8_t sh3510_apply_requested_fets(void)
{
    uint8_t charge_on = 0u;
    uint8_t discharge_on = 0u;
    uint8_t charge_inhibit;
    uint8_t discharge_inhibit;

    if (!sh3673510_control_ready()) return 0u;

    charge_inhibit = charge_blocked();
    discharge_inhibit = discharge_blocked();

    if (s_output_enabled && sh3510_outputs_healthy())
    {
        charge_on = s_requested_charge_on ? 1u : 0u;
        discharge_on = s_requested_discharge_on ? 1u : 0u;

        /*
         * Common-port reverse-direction recovery:
         * - charge protection may close CHG, but a verified DSGING state must
         *   still be allowed to reopen CHG so discharge does not stay on the
         *   body diode;
         * - discharge protection is symmetric for a verified CHGING state.
         *
         * BSTATUS2 is generated by the AFE direction detector, so this path is
         * not a blind periodic retry. If both directions are inhibited, neither
         * side is force-opened.
         */
        if (charge_inhibit)
            charge_on = (!discharge_inhibit && discharge_on &&
                         (s_bstatus2 & SH3673520_BSTATUS2_DSGING_MASK)) ? 1u : 0u;
        if (discharge_inhibit)
            discharge_on = (!charge_inhibit && charge_on &&
                            (s_bstatus2 & SH3673520_BSTATUS2_CHGING_MASK)) ? 1u : 0u;
    }

    /* Do not rewrite the same command every 200 ms. A hardware protection may
     * legitimately change the actual FET output while the requested command
     * remains unchanged; repeated software writes would fight that behavior. */
    if (s_fet_command_valid &&
        s_last_charge_command == charge_on &&
        s_last_discharge_command == discharge_on)
        return 1u;

    if (!sh3673510_control_set_fets(charge_on, discharge_on))
    {
        note_comm_error();
        s_fet_command_valid = 0u;
        return 0u;
    }

    s_last_charge_command = charge_on;
    s_last_discharge_command = discharge_on;
    s_fet_command_valid = 1u;
    return 1u;
}

static void publish_hw_status(const sh3673510_control_status_t *s)
{
#if SH3673510_HW_PROTECT_ENABLE
    uint8_t chg_flag1;
    uint8_t dsg_flag1;
    uint8_t chg_flag2;
    uint8_t dsg_flag2;
#endif

    if (s == 0) return;
    s_bstatus2 = s->bstatus2;
    g_bms_system_status.bits.b1Status_MOS_CHG =
        (s->bstatus1 & SH3673520_BSTATUS1_CHG_FET_MASK) ? 1u : 0u;
    g_bms_system_status.bits.b1Status_MOS_DSG =
        (s->bstatus1 & SH3673520_BSTATUS1_DSG_FET_MASK) ? 1u : 0u;
    s_hw_afe_error = (s->bstatus1 & SH3673520_BSTATUS1_E2P_ERR_MASK) ? 1u : 0u;
    if (s_hw_afe_error && !bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);

#if SH3673510_HW_PROTECT_ENABLE
    chg_flag1 = (uint8_t)(s->flag1 & (SH3673520_FLAG1_OV_MASK |
                                      SH3673520_FLAG1_OCC_MASK |
                                      SH3673520_FLAG1_SC_MASK));
    dsg_flag1 = (uint8_t)(s->flag1 & (SH3673520_FLAG1_UV_MASK |
                                      SH3673520_FLAG1_OCD1_MASK |
                                      SH3673520_FLAG1_OCD2_MASK |
                                      SH3673520_FLAG1_SC_MASK));
    chg_flag2 = (uint8_t)(s->flag2 & (SH3673520_FLAG2_OTC_MASK |
                                      SH3673520_FLAG2_UTC_MASK |
                                      SH3673520_FLAG2_WDT_MASK));
    dsg_flag2 = (uint8_t)(s->flag2 & (SH3673520_FLAG2_OTD_MASK |
                                      SH3673520_FLAG2_UTD_MASK |
                                      SH3673520_FLAG2_WDT_MASK));
    s_hw_charge_protect = (chg_flag1 || chg_flag2) ? 1u : 0u;
    s_hw_discharge_protect = (dsg_flag1 || dsg_flag2) ? 1u : 0u;
#else
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    s_short_latched = 0u;
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    bms_error_clear(BMS_ERROR_DSG_SHORT);
    bms_error_clear(BMS_ERROR_CBC_DSG);
#endif

    /* RST1 means RAM configuration returned to reset defaults; RST2 means the
     * LDO2/SPI domain reset. Never just clear these diagnostics and continue.
     * Inhibit outputs and re-apply the full verified AFE profile first. */
    if ((s->flag1 & SH3673520_FLAG1_RST1_MASK) ||
        (s->flag2 & SH3673520_FLAG2_RST2_MASK)) {
        s_afe_reconfigure_required = 1u;
        s_output_inhibit = 1u;
        s_valid_snapshot_streak = 0u;
    }

#if SH3673510_HW_PROTECT_ENABLE
    if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
        /* A latched hardware flag remains set throughout LOADOFF qualification.
         * Re-observing it must not restart the recovery window every frame. */
        if (!s_short_latched) {
            s_short_clear_pending = 0u;
            s_short_release_count = 0u;
        }
        s_short_latched = 1u;
        if (!bms_error_get(BMS_ERROR_DSG_SHORT)) bms_error_raise(BMS_ERROR_DSG_SHORT);
        if (!bms_error_get(BMS_ERROR_CBC_DSG)) bms_error_raise(BMS_ERROR_CBC_DSG);
    }
#endif
}

#if SH3673510_HW_PROTECT_ENABLE
static void merge_hw_protection_faults(const sh3673510_control_status_t *s)
{
    union MDLCHGFAULT_REG *f;
    if (s == 0) return;

    /* Hardware protection may act before the 200 ms software sample sees the
     * violating value. Mirror the latched AFE protection into the Third-level
     * report so the host never shows "no protection" while a MOS is blocked. */
    f = &g_stCellInfoReport.unMdlFault_Third;
    if (s->flag1 & SH3673520_FLAG1_OV_MASK) f->bits.b1CellOvp = 1u;
    if (s->flag1 & SH3673520_FLAG1_UV_MASK) f->bits.b1CellUvp = 1u;
    if (s->flag1 & SH3673520_FLAG1_OCC_MASK) f->bits.b1IchgOcp = 1u;
    if (s->flag1 & (SH3673520_FLAG1_OCD1_MASK | SH3673520_FLAG1_OCD2_MASK))
        f->bits.b1IdischgOcp = 1u;
    if (s->flag2 & SH3673520_FLAG2_OTC_MASK) f->bits.b1CellChgOtp = 1u;
    if (s->flag2 & SH3673520_FLAG2_UTC_MASK) f->bits.b1CellChgUtp = 1u;
    if (s->flag2 & SH3673520_FLAG2_OTD_MASK) f->bits.b1CellDischgOtp = 1u;
    if (s->flag2 & SH3673520_FLAG2_UTD_MASK) f->bits.b1CellDischgUtp = 1u;
}

static void service_short_recovery(const sh3673510_control_status_t *s)
{
    if ((s == 0) || !s_short_latched) return;

    if (s_short_clear_pending) {
        if (((s->flag1 & SH3673520_FLAG1_SC_MASK) == 0u) &&
            (s->bstatus2 & SH3673520_BSTATUS2_LOADOFF_MASK)) {
            s_short_latched = 0u;
            s_short_clear_pending = 0u;
            s_short_release_count = 0u;
            bms_error_clear(BMS_ERROR_DSG_SHORT);
            bms_error_clear(BMS_ERROR_CBC_DSG);
            return;
        }
        /* SC reassertion or load reattachment invalidates the pending clear.
         * Require a complete new LOADOFF window before another attempt. */
        s_short_clear_pending = 0u;
        s_short_release_count = 0u;
    }

    if (s->bstatus2 & SH3673520_BSTATUS2_LOADOFF_MASK) {
        if (s_short_release_count < SH3510_SHORT_RELEASE_SAMPLES) ++s_short_release_count;
    } else {
        s_short_release_count = 0u;
    }

    if (!s_short_clear_pending && s_short_release_count >= SH3510_SHORT_RELEASE_SAMPLES) {
        if (sh3673510_control_clear_flag1(SH3673520_FLAG1_SC_MASK)) {
            s_short_clear_pending = 1u;
            s_short_release_count = 0u;
        }
    }
}

static uint8_t hw_recovery_stable(sh3510_hw_recovery_id_t id,
                                  uint8_t safe,
                                  uint16_t filter_10ms)
{
    uint16_t needed;
    if ((uint8_t)id >= (uint8_t)HW_REC_COUNT) return 0u;
    if (!safe) {
        s_hw_recovery_count[id] = 0u;
        return 0u;
    }
    needed = filter_samples(filter_10ms);
    if (s_hw_recovery_count[id] < needed) ++s_hw_recovery_count[id];
    return (s_hw_recovery_count[id] >= needed) ? 1u : 0u;
}

static void service_hw_flag_recovery(const sh3673510_control_status_t *s)
{
    sh3673510_protection_actual_t actual;
    bms_afe_hw_profile_t hw;
    uint8_t actual_ok;
    uint8_t c1 = 0u, c2 = 0u;
    uint16_t bat_min = 0u, bat_max = 0u;
    uint8_t bat_temp_ok;
    uint8_t dsg_ocp_release_ok;
    if (s == 0) return;
    if (!bms_afe_hw_profile_get(&hw)) return;

    actual_ok = sh3673510_control_get_protection_actual(&actual);
    /* Current naturally becomes zero after OCD turns DSG off, so current alone
     * is not proof that the external overload disappeared. Require either
     * stable LOADOFF or a real charge-direction state before clearing OCD. */
    dsg_ocp_release_ok = (uint8_t)(((s->bstatus2 & SH3673520_BSTATUS2_LOADOFF_MASK) ||
                                    (s->bstatus2 & SH3673520_BSTATUS2_CHGING_MASK)) ? 1u : 0u);

    /* Reset/wake events are diagnostic. RST1/RST2 are intentionally NOT
     * cleared here: service_afe_reconfiguration() owns those states. */
    c1 |= (uint8_t)(s->flag1 & SH3673520_FLAG1_WK_MASK);
    if (s->flag2 & SH3673520_FLAG2_WDT_MASK) c2 |= SH3673520_FLAG2_WDT_MASK;

    if (s->flag1 & SH3673520_FLAG1_OV_MASK) {
        if (hw_recovery_stable(HW_REC_OV,
                actual_ok &&
                g_stCellInfoReport.u16VCellMax <= hw.cov_recover_mv &&
                g_stCellInfoReport.u16VCellMax < actual.ov_mv,
                (u16)((hw.cov_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OV_MASK;
    } else s_hw_recovery_count[HW_REC_OV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_UV_MASK) {
        if (hw_recovery_stable(HW_REC_UV,
                actual_ok &&
                g_stCellInfoReport.u16VCellMin >= hw.cuv_recover_mv &&
                g_stCellInfoReport.u16VCellMin > actual.uv_mv,
                (u16)((hw.cuv_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_UV_MASK;
    } else s_hw_recovery_count[HW_REC_UV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD1_MASK) {
        if (hw_recovery_stable(HW_REC_OCD1,
                actual_ok && dsg_ocp_release_ok &&
                g_stCellInfoReport.u16IDischg <= hw.ocd_recover_a10 &&
                g_stCellInfoReport.u16IDischg < actual.ocd1_a10,
                (u16)((hw.ocd_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OCD1_MASK;
    } else s_hw_recovery_count[HW_REC_OCD1] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD2_MASK) {
        if (hw_recovery_stable(HW_REC_OCD2,
                actual_ok && dsg_ocp_release_ok &&
                g_stCellInfoReport.u16IDischg <= hw.ocd_recover_a10 &&
                g_stCellInfoReport.u16IDischg < actual.ocd2_a10,
                (u16)((hw.ocd_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OCD2_MASK;
    } else s_hw_recovery_count[HW_REC_OCD2] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCC_MASK) {
        if (hw_recovery_stable(HW_REC_OCC,
                actual_ok &&
                g_stCellInfoReport.u16Ichg <= hw.occ_recover_a10 &&
                g_stCellInfoReport.u16Ichg < actual.occ_a10,
                (u16)((hw.occ_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OCC_MASK;
    } else s_hw_recovery_count[HW_REC_OCC] = 0u;

    /* SCONF6 enables AFE temperature protection only for TS1/TS2, therefore
     * hardware TEMP flag recovery must use the battery sensors only. */
    bat_temp_ok = battery_temperature_snapshot(&bat_min, &bat_max);
    if (s->flag2 & SH3673520_FLAG2_OTC_MASK) {
        if (hw_recovery_stable(HW_REC_OTC,
                bat_temp_ok && bat_max <= hw.chg_ot_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_OTC_MASK;
    } else s_hw_recovery_count[HW_REC_OTC] = 0u;

    if (s->flag2 & SH3673520_FLAG2_OTD_MASK) {
        if (hw_recovery_stable(HW_REC_OTD,
                bat_temp_ok && bat_max <= hw.dsg_ot_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_OTD_MASK;
    } else s_hw_recovery_count[HW_REC_OTD] = 0u;

    if (s->flag2 & SH3673520_FLAG2_UTC_MASK) {
        if (hw_recovery_stable(HW_REC_UTC,
                bat_temp_ok && bat_min >= hw.chg_ut_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_UTC_MASK;
    } else s_hw_recovery_count[HW_REC_UTC] = 0u;

    if (s->flag2 & SH3673520_FLAG2_UTD_MASK) {
        if (hw_recovery_stable(HW_REC_UTD,
                bat_temp_ok && bat_min >= hw.dsg_ut_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_UTD_MASK;
    } else s_hw_recovery_count[HW_REC_UTD] = 0u;

    /* SC is deliberately excluded. It is released only after stable LOADOFF. */
    if (c1) (void)sh3673510_control_clear_flag1(c1);
    if (c2) (void)sh3673510_control_clear_flag2(c2);
}

#endif

static uint8_t service_afe_reconfiguration(void)
{
    if (!s_afe_reconfigure_required) return 1u;

    /* Configuration and direct OFF below bypass the normal command cache. */
    s_fet_command_valid = 0u;
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    s_snapshot_valid = 0u;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    (void)sh3673510_control_set_balance(0u);
    s_balance_mask = 0u;
    (void)sh3673510_control_set_fets(0u, 0u);
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));

    if (!sh3673510_control_init()) return 0u;
    if (!sh3673510_control_clear_flag1(SH3673520_FLAG1_RST1_MASK)) return 0u;
    if (!sh3673510_control_clear_flag2(SH3673520_FLAG2_RST2_MASK)) return 0u;

    s_afe_reconfigure_required = 0u;
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    return 1u;
}

static uint8_t publish_measurements(void)
{
    int32_t cell[SH3673510_D011_CELL_COUNT];
    int32_t pack_mv;
    int32_t current_ma;
    sh3673520_current_raw_t current;
    sh3673520_temperature_raw_t temp;
    sh3673510_control_status_t status;
    bms_sw_protection_inputs_t sw;
    uint16_t max_mv = 0u, min_mv = 0xFFFFu;
    uint16_t bat_temp_min = 0u, bat_temp_max = 0u;
    uint8_t max_pos = 0u, min_pos = 0u, i;

    if (!sh3673510_control_wake()) return 0u;
    if (SH3673520_ReadCellVoltages(cell, SH3673510_D011_CELL_COUNT) != SH3673520_OK) return 0u;
    if (SH3673520_ReadPackVoltage(&pack_mv) != SH3673520_OK) return 0u;
    if (SH3673520_ReadCurrent(&current) != SH3673520_OK) return 0u;
    if (SH3673520_ReadTemperatures(&temp) != SH3673520_OK) return 0u;
    if (!sh3673510_control_read_status(&status)) return 0u;
    if (SH3673520_CurrentRawToMilliAmp(current.cadc_raw,
        SH3673510_D011_SHUNT_UOHM, &current_ma) != SH3673520_OK) return 0u;

    for (i = 0u; i < SH3673510_D011_CELL_COUNT; ++i) {
        uint16_t mv;
        if (cell[i] < 0L || cell[i] > 65535L) return 0u;
        mv = (uint16_t)cell[i];
        g_stCellInfoReport.u16VCell[i] = mv;
        if (mv > max_mv) { max_mv = mv; max_pos = (uint8_t)(i + 1u); }
        if (mv < min_mv) { min_mv = mv; min_pos = (uint8_t)(i + 1u); }
    }
    for (i = SH3673510_D011_CELL_COUNT; i < 32u; ++i) g_stCellInfoReport.u16VCell[i] = 0u;
    g_stCellInfoReport.u16VCellMax = max_mv;
    g_stCellInfoReport.u16VCellMin = min_mv;
    g_stCellInfoReport.u16VCellMaxPosition = max_pos;
    g_stCellInfoReport.u16VCellMinPosition = min_pos;
    g_stCellInfoReport.u16VCellDelta = (uint16_t)(max_mv - min_mv);

    if (pack_mv < 0L) pack_mv = 0L;
    s_aux.pack_voltage_mv = (uint32_t)pack_mv;
    g_stCellInfoReport.u16VCellTotle = (uint16_t)(((uint32_t)pack_mv + 5u) / 10u);

    s_aux.current_ma = current_ma;
    s_aux.sample_tick_32k = pm_get_32k_tick();

    if (current_ma < 0L) {
        uint32_t ma = (uint32_t)(-current_ma);
        g_stCellInfoReport.u16IDischg = (uint16_t)((ma + 50u) / 100u);
        g_stCellInfoReport.u16Ichg = 0u;
    } else {
        uint32_t ma = (uint32_t)current_ma;
        g_stCellInfoReport.u16Ichg = (uint16_t)((ma + 50u) / 100u);
        g_stCellInfoReport.u16IDischg = 0u;
    }

    memset(s_ntc_valid, 0, sizeof(s_ntc_valid));
    memset(s_ntc_ohm, 0, sizeof(s_ntc_ohm));
    for (i = 0u; i < 4u; ++i) {
        uint32_t r;
        if (SH3673520_NtcRawToOhm(temp.external_raw[i], &r) == SH3673520_OK &&
            r >= 500u && r <= 300000u) { s_ntc_valid[i] = 1u; s_ntc_ohm[i] = r; }
    }
    g_stCellInfoReport.u16Temperature[AFE1_TEMP1] =
        s_ntc_valid[SH3673510_D011_BAT_NTC1_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_BAT_NTC1_INDEX]) : 0u;
    g_stCellInfoReport.u16Temperature[AFE1_TEMP2] =
        s_ntc_valid[SH3673510_D011_BAT_NTC2_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_BAT_NTC2_INDEX]) : 0u;
#if SH3673510_PRODUCT_HEATER_NTC_SUPPORTED
    g_stCellInfoReport.u16Temperature[AFE1_TEMP3] =
        s_ntc_valid[SH3673510_D011_HEATER_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_HEATER_NTC_INDEX]) : 0u;
#else
    g_stCellInfoReport.u16Temperature[AFE1_TEMP3] = 0u;
#endif
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] =
        s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_MOS_NTC_INDEX]) : 0u;
#else
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] = 0u;
#endif

    /*
     * Realtime max/min temperature is the validated battery range TS1/TS2.
     * D014 marks TS3 NC and does not yet qualify TS4/RN4 as a 10K NTC, so
     * those auxiliary channels are not folded into battery extrema or
     * published as trusted temperatures until BOM/board evidence exists.
     * Zero remains the legacy invalid/sensor-break sentinel.
     */
    if (battery_temperature_snapshot(&bat_temp_min, &bat_temp_max)) {
        g_stCellInfoReport.u16TempMin = bat_temp_min;
        g_stCellInfoReport.u16TempMax = bat_temp_max;
    } else {
        g_stCellInfoReport.u16TempMin = 0u;
        g_stCellInfoReport.u16TempMax = 0u;
    }

    if (s_ntc_valid[SH3673510_D011_BAT_NTC1_INDEX] &&
        s_ntc_valid[SH3673510_D011_BAT_NTC2_INDEX])
        s_aux.battery_ntc_100ohm =
            ((s_ntc_ohm[SH3673510_D011_BAT_NTC1_INDEX] < s_ntc_ohm[SH3673510_D011_BAT_NTC2_INDEX]) ?
             s_ntc_ohm[SH3673510_D011_BAT_NTC1_INDEX] : s_ntc_ohm[SH3673510_D011_BAT_NTC2_INDEX]) / 100u;
    else if (s_ntc_valid[SH3673510_D011_BAT_NTC1_INDEX])
        s_aux.battery_ntc_100ohm = s_ntc_ohm[SH3673510_D011_BAT_NTC1_INDEX] / 100u;
    else if (s_ntc_valid[SH3673510_D011_BAT_NTC2_INDEX])
        s_aux.battery_ntc_100ohm = s_ntc_ohm[SH3673510_D011_BAT_NTC2_INDEX] / 100u;
    else s_aux.battery_ntc_100ohm = 0u;
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
    s_aux.mos_ntc_100ohm = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ?
        s_ntc_ohm[SH3673510_D011_MOS_NTC_INDEX] / 100u : 0u;
#else
    s_aux.mos_ntc_100ohm = 0u;
#endif
    s_aux.battery_ntc_mv = legacy_adc_mv(s_aux.battery_ntc_100ohm * 100u);
    s_aux.mos_ntc_mv = legacy_adc_mv(s_aux.mos_ntc_100ohm * 100u);

    publish_hw_status(&status);
    if (!s_afe_reconfigure_required) {
        memset(&sw, 0, sizeof(sw));
        sw.battery_temp_valid = battery_temperature_snapshot(&sw.battery_temp_min,
                                                              &sw.battery_temp_max);
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
        sw.mos_temp_valid = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ? 1u : 0u;
        if (sw.mos_temp_valid)
            sw.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
#else
        sw.mos_temp_valid = 0u;
        sw.mos_temp = 0u;
#endif
#if SH3673510_SW_PROTECT_ENABLE
        bms_sw_protection_update(&sw);
#else
        bms_sw_protection_clear();
#endif
#if SH3673510_HW_PROTECT_ENABLE
        merge_hw_protection_faults(&status);
        service_short_recovery(&status);
        service_hw_flag_recovery(&status);
#endif
        bms_sw_protection_record_fault_edges();
    }
    return 1u;
}

void sh3673510_bms_afe_init(void)
{
    bms_sw_protection_init();
    memset(&s_aux, 0, sizeof(s_aux));
    s_snapshot_valid = 0u;
    s_balance_mask = 0u;
    s_hw_afe_error = 0u;
    s_requested_charge_on = 0u;
    s_requested_discharge_on = 0u;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    /* Preserve s_short_latched across AFE communication reinitialization. */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    s_afe_reconfigure_required = 0u;
    s_bstatus2 = 0u;
    s_fet_command_valid = 0u;
    s_last_charge_command = 0u;
    s_last_discharge_command = 0u;
    sh3673510_board_force_heater_fuse_safe();
    sh3673510_board_set_heater(0u);
    if (!sh3673510_control_init()) { note_comm_error(); return; }
    note_comm_ok();
}

void sh3673510_bms_afe_sample(void)
{
    sh3673510_board_force_heater_fuse_safe();
    if (!publish_measurements()) {
        /* Common bms_afe_guard owns OFF, WDT silence and bounded re-init. */
        s_snapshot_valid = 0u;
        note_comm_error();
        return;
    }

    if (s_afe_reconfigure_required) {
        if (!service_afe_reconfiguration()) note_comm_error();
        return; /* require fresh post-configuration measurements on the next cycle */
    }

    s_snapshot_valid = 1u;
    if (s_valid_snapshot_streak < SH3510_VALID_SNAPSHOT_RELEASE_COUNT) ++s_valid_snapshot_streak;
    if (s_valid_snapshot_streak >= SH3510_VALID_SNAPSHOT_RELEASE_COUNT) s_output_inhibit = 0u;
    note_comm_ok();
    /* Common features owns heater/balance; common guard owns final FET apply. */
}

uint8_t sh3673510_bms_afe_apply_protection_config(void)
{
    if (!sh3673510_control_ready()) return 0u;
    if (!sh3673510_control_apply_protection()) { note_comm_error(); return 0u; }
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    return 1u;
}

uint8_t sh3673510_bms_afe_set_fets(uint8_t requested_charge_on,
                                   uint8_t requested_discharge_on)
{
    s_requested_charge_on = requested_charge_on ? 1u : 0u;
    s_requested_discharge_on = requested_discharge_on ? 1u : 0u;
    return sh3510_apply_requested_fets();
}

void sh3673510_bms_afe_set_output_enabled(uint8_t enabled)
{
    s_output_enabled = enabled ? 1u : 0u;
    if (!s_output_enabled) {
        s_fet_command_valid = 0u;
        if (sh3673510_control_ready()) {
            (void)sh3673510_control_set_balance(0u);
            if (!sh3673510_control_set_fets(0u, 0u)) note_comm_error();
        }
    } else if (s_snapshot_valid) {
        (void)sh3510_apply_requested_fets();
    }
}

uint8_t sh3673510_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m)
{
    if (m == 0) return 0u;
    if (!s_snapshot_valid) { memset(m, 0, sizeof(*m)); return 0u; }
    *m = s_aux;
    return 1u;
}

uint8_t sh3673510_bms_afe_get_fet_diagnostics(uint8_t *command_bits,
                                               uint8_t *command_valid,
                                               uint8_t *driver_bits,
                                               uint8_t *driver_valid)
{
    if ((command_bits == 0) || (command_valid == 0) ||
        (driver_bits == 0) || (driver_valid == 0)) return 0u;
    *command_bits = (uint8_t)((s_last_charge_command ? 1u : 0u) |
                              (s_last_discharge_command ? 2u : 0u));
    *command_valid = s_fet_command_valid;
    *driver_bits = (uint8_t)((g_bms_system_status.bits.b1Status_MOS_CHG ? 1u : 0u) |
                             (g_bms_system_status.bits.b1Status_MOS_DSG ? 2u : 0u));
    *driver_valid = s_snapshot_valid;
    return 1u;
}

uint8_t sh3673510_bms_afe_sleep(void)
{
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    s_fet_command_valid = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    /* Even an aborted transition invalidates the old driver/sample evidence. */
    s_snapshot_valid = 0u;
    if (!sh3673510_control_sleep()) {
        note_comm_error();
        return 0u;
    }
    s_balance_mask = 0u;
    return 1u;
}
