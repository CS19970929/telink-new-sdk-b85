#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
BMS = VENDOR / "sh3673510_bms.c"
TEST = ROOT / "tests" / "sh3673510_d011_integration_check.py"
DOC = ROOT / "docs" / "SH36735XX_AFE_REGISTER_CONFIG.md"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        if new in text:
            return text
        raise RuntimeError(f"{label}: expected block not found")
    return text.replace(old, new, 1)


bms = BMS.read_text(encoding="utf-8")

bms = replace_once(bms, '''typedef enum {
    F_CELL_OV = 0, F_CELL_UV, F_PACK_OV, F_PACK_UV,
    F_CHG_OC, F_DSG_OC, F_CHG_OT, F_CHG_UT,
    F_DSG_OT, F_DSG_UT, F_MOS_OT, F_VDELTA, F_COUNT
} sh3510_filter_id_t;

static sh3510_filter_t s_filter[SH3510_LEVEL_COUNT][F_COUNT];
''', '''typedef enum {
    F_CELL_OV = 0, F_CELL_UV, F_PACK_OV, F_PACK_UV,
    F_CHG_OC, F_DSG_OC, F_CHG_OT, F_CHG_UT,
    F_DSG_OT, F_DSG_UT, F_MOS_OT, F_VDELTA, F_COUNT
} sh3510_filter_id_t;

typedef enum {
    HW_REC_OV = 0, HW_REC_UV, HW_REC_OCD1, HW_REC_OCD2,
    HW_REC_OCC, HW_REC_OTC, HW_REC_OTD, HW_REC_UTC, HW_REC_UTD,
    HW_REC_COUNT
} sh3510_hw_recovery_id_t;

static sh3510_filter_t s_filter[SH3510_LEVEL_COUNT][F_COUNT];
''', "add hardware recovery enum")

bms = replace_once(bms, '''static uint8_t s_short_latched;
static uint8_t s_short_clear_pending;
static uint16_t s_short_release_count;
''', '''static uint8_t s_short_latched;
static uint8_t s_short_clear_pending;
static uint16_t s_short_release_count;
static uint16_t s_hw_recovery_count[HW_REC_COUNT];
static uint8_t s_hw_charge_protect;
static uint8_t s_hw_discharge_protect;
static uint8_t s_afe_reconfigure_required;
static uint8_t s_heater_mos_overtemp;
''', "add hardware recovery state")

old_temp_snapshot = '''static uint8_t temperature_snapshot(uint16_t *bat_min,
                                    uint16_t *bat_max,
                                    uint16_t *mos_temp)
{
    uint16_t t1;
    uint16_t t2;
    if (bat_min == 0 || bat_max == 0 || mos_temp == 0) return 0u;
    if (!s_ntc_valid[0] || !s_ntc_valid[1] || !s_ntc_valid[3]) return 0u;
    t1 = g_stCellInfoReport.u16Temperature[AFE1_TEMP1];
    t2 = g_stCellInfoReport.u16Temperature[AFE1_TEMP2];
    *bat_min = (t1 < t2) ? t1 : t2;
    *bat_max = (t1 > t2) ? t1 : t2;
    *mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    return 1u;
}
'''
new_temp_snapshot = '''static uint8_t battery_temperature_snapshot(uint16_t *bat_min,
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

static uint8_t temperature_snapshot(uint16_t *bat_min,
                                    uint16_t *bat_max,
                                    uint16_t *mos_temp)
{
    if (mos_temp == 0) return 0u;
    if (!battery_temperature_snapshot(bat_min, bat_max)) return 0u;
    if (!s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX]) return 0u;
    *mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    return 1u;
}
'''
bms = replace_once(bms, old_temp_snapshot, new_temp_snapshot, "split battery and MOS temperature snapshots")

old_blocks = '''static uint8_t charge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) || s_heater_on) ? 1u : 0u;
}

static uint8_t discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            s_short_latched ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}
'''
new_blocks = '''static uint8_t charge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (s_hw_charge_protect ||
            f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) || s_heater_on) ? 1u : 0u;
}

static uint8_t discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (s_hw_discharge_protect ||
            f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            s_short_latched ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}
'''
bms = replace_once(bms, old_blocks, new_blocks, "gate FETs with hardware protection flags")

old_heater = '''static void apply_heater(void)
{
    uint16_t bat_min, bat_max, mos_temp;
    uint8_t temp_ok = temperature_snapshot(&bat_min, &bat_max, &mos_temp);
    uint8_t on = s_heater_on;
    (void)bat_max; (void)mos_temp;

    if (!s_output_enabled || !sh3673510_board_wake_active() ||
        !temp_ok || bms_error_get(BMS_ERROR_AFE1)) on = 0u;
    else if (on && bat_min >= g_tParam.protect.u16TchgUTp_Rcv) on = 0u;
    else if (!on && bat_min <= g_tParam.protect.u16TchgUTp_Third) on = 1u;

    if (on != s_heater_on) {
        s_heater_on = on;
        sh3673510_board_set_heater(on);
    }
    g_bms_system_status.bits.b1Status_Heat = s_heater_on;
}
'''
new_heater = '''static void apply_heater(void)
{
    uint16_t bat_min = 0u, bat_max = 0u;
    uint16_t heater_mos_temp = 0u;
    uint8_t battery_temp_ok = battery_temperature_snapshot(&bat_min, &bat_max);
    uint8_t heater_temp_ok = s_ntc_valid[SH3673510_D011_HEATER_NTC_INDEX];
    uint8_t on = s_heater_on;
    (void)bat_max;

    if (heater_temp_ok)
        heater_mos_temp = g_stCellInfoReport.u16Temperature[AFE1_TEMP3];

    /* TS3 is the reversible heater-MOS safety cutoff. Until a dedicated
     * heater-MOS parameter group is added, reuse the existing MOS third/recover
     * thresholds. This path NEVER authorizes the irreversible PB5 fuse trigger. */
    if (!heater_temp_ok) {
        s_heater_mos_overtemp = 1u;
    } else if (s_heater_mos_overtemp) {
        if (heater_mos_temp <= g_tParam.protect.u16TmosOTp_Rcv)
            s_heater_mos_overtemp = 0u;
    } else if (heater_mos_temp >= g_tParam.protect.u16TmosOTp_Third) {
        s_heater_mos_overtemp = 1u;
    }

    if (!heater_temp_ok || s_heater_mos_overtemp) {
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        on = 0u;
    } else {
        bms_error_clear(BMS_ERROR_HEAT);
    }

    if (!s_output_enabled || !sh3673510_board_wake_active() ||
        !battery_temp_ok || bms_error_get(BMS_ERROR_AFE1)) on = 0u;
    else if (on && bat_min >= g_tParam.protect.u16TchgUTp_Rcv) on = 0u;
    else if (!on && !s_heater_mos_overtemp &&
             bat_min <= g_tParam.protect.u16TchgUTp_Third) on = 1u;

    if (on != s_heater_on) {
        s_heater_on = on;
        sh3673510_board_set_heater(on);
    }
    g_bms_system_status.bits.b1Status_Heat = s_heater_on;
}
'''
bms = replace_once(bms, old_heater, new_heater, "add TS3 heater MOS cutoff")

old_publish_hw = '''static void publish_hw_status(const sh3673510_control_status_t *s)
{
    if (s == 0) return;
    g_bms_system_status.bits.b1Status_MOS_CHG =
        (s->bstatus1 & SH3673520_BSTATUS1_CHG_FET_MASK) ? 1u : 0u;
    g_bms_system_status.bits.b1Status_MOS_DSG =
        (s->bstatus1 & SH3673520_BSTATUS1_DSG_FET_MASK) ? 1u : 0u;
    s_hw_afe_error = (s->bstatus1 & SH3673520_BSTATUS1_E2P_ERR_MASK) ? 1u : 0u;
    if (s_hw_afe_error && !bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);

    if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
        s_short_latched = 1u;
        s_short_clear_pending = 0u;
        s_short_release_count = 0u;
        if (!bms_error_get(BMS_ERROR_DSG_SHORT)) bms_error_raise(BMS_ERROR_DSG_SHORT);
        if (!bms_error_get(BMS_ERROR_CBC_DSG)) bms_error_raise(BMS_ERROR_CBC_DSG);
    }
}
'''
new_publish_hw = '''static void publish_hw_status(const sh3673510_control_status_t *s)
{
    uint8_t chg_flag1;
    uint8_t dsg_flag1;
    uint8_t chg_flag2;
    uint8_t dsg_flag2;

    if (s == 0) return;
    g_bms_system_status.bits.b1Status_MOS_CHG =
        (s->bstatus1 & SH3673520_BSTATUS1_CHG_FET_MASK) ? 1u : 0u;
    g_bms_system_status.bits.b1Status_MOS_DSG =
        (s->bstatus1 & SH3673520_BSTATUS1_DSG_FET_MASK) ? 1u : 0u;
    s_hw_afe_error = (s->bstatus1 & SH3673520_BSTATUS1_E2P_ERR_MASK) ? 1u : 0u;
    if (s_hw_afe_error && !bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);

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

    /* RST1 means RAM configuration returned to reset defaults; RST2 means the
     * LDO2/SPI domain reset. Never just clear these diagnostics and continue.
     * Inhibit outputs and re-apply the full verified AFE profile first. */
    if ((s->flag1 & SH3673520_FLAG1_RST1_MASK) ||
        (s->flag2 & SH3673520_FLAG2_RST2_MASK)) {
        s_afe_reconfigure_required = 1u;
        s_output_inhibit = 1u;
        s_valid_snapshot_streak = 0u;
    }

    if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
        s_short_latched = 1u;
        s_short_clear_pending = 0u;
        s_short_release_count = 0u;
        if (!bms_error_get(BMS_ERROR_DSG_SHORT)) bms_error_raise(BMS_ERROR_DSG_SHORT);
        if (!bms_error_get(BMS_ERROR_CBC_DSG)) bms_error_raise(BMS_ERROR_CBC_DSG);
    }
}
'''
bms = replace_once(bms, old_publish_hw, new_publish_hw, "track hardware flags and AFE reset")

old_clear = '''static void clear_recovered_flags(const sh3673510_control_status_t *s)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    uint8_t c1 = 0u, c2 = 0u;
    if (s == 0) return;

    c1 |= (uint8_t)(s->flag1 & (SH3673520_FLAG1_RST1_MASK | SH3673520_FLAG1_WK_MASK));
    if ((s->flag1 & SH3673520_FLAG1_OV_MASK) && !f->b1CellOvp) c1 |= SH3673520_FLAG1_OV_MASK;
    if ((s->flag1 & SH3673520_FLAG1_UV_MASK) && !f->b1CellUvp) c1 |= SH3673520_FLAG1_UV_MASK;
    if ((s->flag1 & (SH3673520_FLAG1_OCD1_MASK | SH3673520_FLAG1_OCD2_MASK)) && !f->b1IdischgOcp)
        c1 |= (uint8_t)(s->flag1 & (SH3673520_FLAG1_OCD1_MASK | SH3673520_FLAG1_OCD2_MASK));
    if ((s->flag1 & SH3673520_FLAG1_OCC_MASK) && !f->b1IchgOcp) c1 |= SH3673520_FLAG1_OCC_MASK;
    /* SC is deliberately excluded. It is released only by service_short_recovery(). */

    c2 |= (uint8_t)(s->flag2 & SH3673520_FLAG2_RST2_MASK);
    if ((s->flag2 & SH3673520_FLAG2_OTC_MASK) && !f->b1CellChgOtp) c2 |= SH3673520_FLAG2_OTC_MASK;
    if ((s->flag2 & SH3673520_FLAG2_OTD_MASK) && !f->b1CellDischgOtp) c2 |= SH3673520_FLAG2_OTD_MASK;
    if ((s->flag2 & SH3673520_FLAG2_UTC_MASK) && !f->b1CellChgUtp) c2 |= SH3673520_FLAG2_UTC_MASK;
    if ((s->flag2 & SH3673520_FLAG2_UTD_MASK) && !f->b1CellDischgUtp) c2 |= SH3673520_FLAG2_UTD_MASK;
    if (s->flag2 & SH3673520_FLAG2_WDT_MASK) c2 |= SH3673520_FLAG2_WDT_MASK;

    if (c1) (void)sh3673510_control_clear_flag1(c1);
    if (c2) (void)sh3673510_control_clear_flag2(c2);
}
'''
new_clear = '''static uint8_t hw_recovery_stable(sh3510_hw_recovery_id_t id,
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
    uint8_t c1 = 0u, c2 = 0u;
    uint16_t bat_min = 0u, bat_max = 0u;
    uint8_t bat_temp_ok;
    if (s == 0) return;

    /* Reset/wake events are diagnostic. RST1/RST2 are intentionally NOT
     * cleared here: service_afe_reconfiguration() owns those states. */
    c1 |= (uint8_t)(s->flag1 & SH3673520_FLAG1_WK_MASK);
    if (s->flag2 & SH3673520_FLAG2_WDT_MASK) c2 |= SH3673520_FLAG2_WDT_MASK;

    if (s->flag1 & SH3673520_FLAG1_OV_MASK) {
        if (hw_recovery_stable(HW_REC_OV,
                g_stCellInfoReport.u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv,
                g_tParam.protect.u16VcellOvp_Filter)) c1 |= SH3673520_FLAG1_OV_MASK;
    } else s_hw_recovery_count[HW_REC_OV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_UV_MASK) {
        if (hw_recovery_stable(HW_REC_UV,
                g_stCellInfoReport.u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv,
                g_tParam.protect.u16VcellUvp_Filter)) c1 |= SH3673520_FLAG1_UV_MASK;
    } else s_hw_recovery_count[HW_REC_UV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD1_MASK) {
        if (hw_recovery_stable(HW_REC_OCD1,
                g_stCellInfoReport.u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv,
                g_tParam.protect.u16IdsgOcp_Filter)) c1 |= SH3673520_FLAG1_OCD1_MASK;
    } else s_hw_recovery_count[HW_REC_OCD1] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD2_MASK) {
        if (hw_recovery_stable(HW_REC_OCD2,
                g_stCellInfoReport.u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv,
                g_tParam.protect.u16IdsgOcp_Filter)) c1 |= SH3673520_FLAG1_OCD2_MASK;
    } else s_hw_recovery_count[HW_REC_OCD2] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCC_MASK) {
        if (hw_recovery_stable(HW_REC_OCC,
                g_stCellInfoReport.u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv,
                g_tParam.protect.u16IchgOcp_Filter)) c1 |= SH3673520_FLAG1_OCC_MASK;
    } else s_hw_recovery_count[HW_REC_OCC] = 0u;

    /* SCONF6 enables AFE temperature protection only for TS1/TS2, therefore
     * hardware TEMP flag recovery must use the battery sensors only. */
    bat_temp_ok = battery_temperature_snapshot(&bat_min, &bat_max);
    if (s->flag2 & SH3673520_FLAG2_OTC_MASK) {
        if (hw_recovery_stable(HW_REC_OTC,
                bat_temp_ok && bat_max <= g_tParam.protect.u16TChgOTp_Rcv,
                g_tParam.protect.u16TChgOTp_Filter)) c2 |= SH3673520_FLAG2_OTC_MASK;
    } else s_hw_recovery_count[HW_REC_OTC] = 0u;

    if (s->flag2 & SH3673520_FLAG2_OTD_MASK) {
        if (hw_recovery_stable(HW_REC_OTD,
                bat_temp_ok && bat_max <= g_tParam.protect.u16TdischgOTp_Rcv,
                g_tParam.protect.u16TdischgOTp_Filter)) c2 |= SH3673520_FLAG2_OTD_MASK;
    } else s_hw_recovery_count[HW_REC_OTD] = 0u;

    if (s->flag2 & SH3673520_FLAG2_UTC_MASK) {
        if (hw_recovery_stable(HW_REC_UTC,
                bat_temp_ok && bat_min >= g_tParam.protect.u16TchgUTp_Rcv,
                g_tParam.protect.u16TchgUTp_Filter)) c2 |= SH3673520_FLAG2_UTC_MASK;
    } else s_hw_recovery_count[HW_REC_UTC] = 0u;

    if (s->flag2 & SH3673520_FLAG2_UTD_MASK) {
        if (hw_recovery_stable(HW_REC_UTD,
                bat_temp_ok && bat_min >= g_tParam.protect.u16TdischgUTp_Rcv,
                g_tParam.protect.u16TdischgUTp_Filter)) c2 |= SH3673520_FLAG2_UTD_MASK;
    } else s_hw_recovery_count[HW_REC_UTD] = 0u;

    /* SC is deliberately excluded. It is released only after stable LOADOFF. */
    if (c1) (void)sh3673510_control_clear_flag1(c1);
    if (c2) (void)sh3673510_control_clear_flag2(c2);
}

static uint8_t service_afe_reconfiguration(void)
{
    if (!s_afe_reconfigure_required) return 1u;

    s_snapshot_valid = 0u;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    s_heater_on = 0u;
    sh3673510_board_set_heater(0u);
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
'''
bms = replace_once(bms, old_clear, new_clear, "replace FLAG recovery state machine")

old_ntc_publish = '''    g_stCellInfoReport.u16Temperature[AFE1_TEMP1] = s_ntc_valid[0] ? ntc_temp(s_ntc_ohm[0]) : 0u;
    g_stCellInfoReport.u16Temperature[AFE1_TEMP2] = s_ntc_valid[1] ? ntc_temp(s_ntc_ohm[1]) : 0u;
    g_stCellInfoReport.u16Temperature[AFE1_TEMP3] = 0u; /* TS3-NC */
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] = s_ntc_valid[3] ? ntc_temp(s_ntc_ohm[3]) : 0u;

    if (s_ntc_valid[0] && s_ntc_valid[1])
        s_aux.battery_ntc_100ohm = ((s_ntc_ohm[0] < s_ntc_ohm[1]) ? s_ntc_ohm[0] : s_ntc_ohm[1]) / 100u;
    else if (s_ntc_valid[0]) s_aux.battery_ntc_100ohm = s_ntc_ohm[0] / 100u;
    else if (s_ntc_valid[1]) s_aux.battery_ntc_100ohm = s_ntc_ohm[1] / 100u;
    else s_aux.battery_ntc_100ohm = 0u;
    s_aux.mos_ntc_100ohm = s_ntc_valid[3] ? s_ntc_ohm[3] / 100u : 0u;
'''
new_ntc_publish = '''    g_stCellInfoReport.u16Temperature[AFE1_TEMP1] =
        s_ntc_valid[SH3673510_D011_BAT_NTC1_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_BAT_NTC1_INDEX]) : 0u;
    g_stCellInfoReport.u16Temperature[AFE1_TEMP2] =
        s_ntc_valid[SH3673510_D011_BAT_NTC2_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_BAT_NTC2_INDEX]) : 0u;
    g_stCellInfoReport.u16Temperature[AFE1_TEMP3] =
        s_ntc_valid[SH3673510_D011_HEATER_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_HEATER_NTC_INDEX]) : 0u;
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] =
        s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_MOS_NTC_INDEX]) : 0u;

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
    s_aux.mos_ntc_100ohm = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ?
        s_ntc_ohm[SH3673510_D011_MOS_NTC_INDEX] / 100u : 0u;
'''
bms = replace_once(bms, old_ntc_publish, new_ntc_publish, "publish TS3 and use NTC index macros")

bms = replace_once(bms, '''    publish_hw_status(&status);
    update_faults();
    service_short_recovery(&status);
    clear_recovered_flags(&status);
    return 1u;
''', '''    publish_hw_status(&status);
    if (!s_afe_reconfigure_required) {
        update_faults();
        service_short_recovery(&status);
        service_hw_flag_recovery(&status);
    }
    return 1u;
''', "route hardware recovery")

bms = replace_once(bms, '''    /* Preserve s_short_latched across AFE communication reinitialization. */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    sh3673510_board_force_heater_fuse_safe();
''', '''    /* Preserve s_short_latched across AFE communication reinitialization. */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    s_afe_reconfigure_required = 0u;
    s_heater_mos_overtemp = 0u;
    sh3673510_board_force_heater_fuse_safe();
''', "initialize new safety state")

bms = replace_once(bms, '''    s_snapshot_valid = 1u;
    if (s_valid_snapshot_streak < SH3510_VALID_SNAPSHOT_RELEASE_COUNT) ++s_valid_snapshot_streak;
''', '''    if (s_afe_reconfigure_required) {
        if (!service_afe_reconfiguration()) note_comm_error();
        return; /* require fresh post-configuration measurements on the next cycle */
    }

    s_snapshot_valid = 1u;
    if (s_valid_snapshot_streak < SH3510_VALID_SNAPSHOT_RELEASE_COUNT) ++s_valid_snapshot_streak;
''', "service AFE reset before releasing outputs")

bms = replace_once(bms, '''            if (sh3673510_control_init()) note_comm_ok();
''', '''            if (sh3673510_control_init()) {
                memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
                s_hw_charge_protect = 0u;
                s_hw_discharge_protect = 0u;
                note_comm_ok();
            }
''', "reset recovery state after communication reinit")

bms = replace_once(bms, '''uint8_t sh3673510_bms_afe_apply_protection_config(void)
{
    if (!sh3673510_control_ready()) return 0u;
    if (!sh3673510_control_apply_protection()) { note_comm_error(); return 0u; }
    return 1u;
}
''', '''uint8_t sh3673510_bms_afe_apply_protection_config(void)
{
    if (!sh3673510_control_ready()) return 0u;
    if (!sh3673510_control_apply_protection()) { note_comm_error(); return 0u; }
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    return 1u;
}
''', "reset recovery qualification after protection parameter update")

bms = replace_once(bms, '''void sh3673510_bms_afe_sleep(void)
{
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
''', '''void sh3673510_bms_afe_sleep(void)
{
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
''', "reset recovery state on sleep")

BMS.write_text(bms, encoding="utf-8")

# Strengthen the permanent integration contract.
test = TEST.read_text(encoding="utf-8")
test = replace_once(test, '''require(bms, "s_requested_charge_on")
''', '''require(bms, "s_requested_charge_on")
require(bms, "service_hw_flag_recovery")
require(bms, "hw_recovery_stable")
require(bms, "service_afe_reconfiguration")
require(bms, "s_hw_charge_protect")
require(bms, "s_hw_discharge_protect")
require(bms, "s_afe_reconfigure_required")
require(bms, "s_heater_mos_overtemp")
require(bms, "BMS_ERROR_HEAT")
require(bms, "SH3673510_D011_HEATER_NTC_INDEX")
require(bms, "g_stCellInfoReport.u16Temperature[AFE1_TEMP3]")
if "clear_recovered_flags" in bms:
    raise AssertionError("AFE flags must use physical-value recovery, not software-third state")
if "TS3-NC" in bms:
    raise AssertionError("D011 TS3 is a fitted 10K heater-MOS NTC, not NC")
''', "add protection recovery contracts")

test = replace_once(test, '''if "FLAG1_SC_MASK" in bms and "u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv" in bms:
    raise AssertionError("short-circuit recovery must not use current-to-zero as load-release proof")
require(bms, "service_short_recovery")
''', '''# Short-circuit recovery must remain a distinct LOADOFF-qualified path even
# though normal OCD1/OCD2 FLAG recovery legitimately uses the OCP recovery current.
short_fn = re.search(r"static void service_short_recovery.*?\n}\n", bms, re.S)
if not short_fn:
    raise AssertionError("missing service_short_recovery")
if "u16IDischg" in short_fn.group(0):
    raise AssertionError("short-circuit recovery must not use discharge current as load-release proof")
require(bms, "service_short_recovery")
''', "scope short recovery invariant correctly")

test = replace_once(test, '''    "static uint16_t s_short_release_count;",
):
''', '''    "static uint16_t s_short_release_count;",
    "static uint16_t s_hw_recovery_count[HW_REC_COUNT];",
    "static uint8_t s_hw_charge_protect;",
    "static uint8_t s_hw_discharge_protect;",
    "static uint8_t s_afe_reconfigure_required;",
    "static uint8_t s_heater_mos_overtemp;",
):
''', "count new safety state exactly once")

extra_contract = '''

# Hardware FLAG recovery must be based on the physical recovery windows, not
# on whether the software Third-level fault happened to become active.
hw_rec = re.search(r"static void service_hw_flag_recovery.*?\n}\n\nstatic uint8_t service_afe_reconfiguration", bms, re.S)
if not hw_rec:
    raise AssertionError("missing hardware FLAG recovery state machine")
hw_text = hw_rec.group(0)
for needle in (
    "u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv",
    "u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv",
    "u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv",
    "u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv",
    "bat_max <= g_tParam.protect.u16TChgOTp_Rcv",
    "bat_min >= g_tParam.protect.u16TchgUTp_Rcv",
):
    require(hw_text, needle)
if "unMdlFault_Third" in hw_text:
    raise AssertionError("hardware FLAG recovery must be independent from software Third-level activity")

heater_fn = re.search(r"static void apply_heater.*?\n}\n\nstatic void apply_balance", bms, re.S)
if not heater_fn:
    raise AssertionError("missing heater control")
heater_text = heater_fn.group(0)
for needle in (
    "SH3673510_D011_HEATER_NTC_INDEX",
    "u16TmosOTp_Third",
    "u16TmosOTp_Rcv",
    "BMS_ERROR_HEAT",
):
    require(heater_text, needle)
if "D011_HEATER_FUSE_TRIGGER_PIN" in heater_text:
    raise AssertionError("reversible heater safety must never actuate the irreversible fuse trigger")
'''
if extra_contract.strip() not in test:
    test = test.replace('\nprint("HS-D011 SH3673510 integration contract: PASS")\n', extra_contract + '\nprint("HS-D011 SH3673510 integration contract: PASS")\n')
TEST.write_text(test, encoding="utf-8")

# Keep the human audit document synchronized with the implemented policy.
doc = DOC.read_text(encoding="utf-8")ndoc = doc
