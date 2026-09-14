#include "bms_afe.h"
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "param.h"
#include "bms_error.h"
#include "bms_state.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include <string.h>

#define SH3510_SAMPLE_MS              200u
#define SH3510_LEVEL_COUNT            3u
#define SH3510_REINIT_TRIGGER         3u
#define SH3510_REINIT_COOLDOWN        25u /* 5 s at 200 ms */
#define SH3510_VALID_SNAPSHOT_RELEASE_COUNT 3u
#define SH3510_SHORT_RELEASE_SAMPLES    10u /* 2 s stable LOADOFF at 200 ms */

typedef struct {
    uint16_t trip_count;
    uint16_t recover_count;
    uint8_t active;
} sh3510_filter_t;

typedef enum {
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
static union MDLCHGFAULT_REG s_prev_fault[SH3510_LEVEL_COUNT];
static bms_afe_aux_measurements_t s_aux;
static uint32_t s_ntc_ohm[4];
static uint8_t s_ntc_valid[4];
static uint8_t s_output_enabled;
static uint8_t s_heater_on;
static uint8_t s_snapshot_valid;
static uint8_t s_comm_failures;
static uint8_t s_reinit_cooldown;
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
static uint8_t s_heater_mos_overtemp;

/* Existing product 10K NTC table: R in 100 ohm, T=(degC+40)*10. */
static const uint16_t s_ntc_table[] = {
    2037u,0u, 1526u,50u, 1161u,100u, 893u,150u, 694u,200u,
    544u,250u, 430u,300u, 342u,350u, 275u,400u, 221u,450u,
    180u,500u, 147u,550u, 121u,600u, 100u,650u, 83u,700u,
    69u,750u, 58u,800u, 49u,850u, 41u,900u, 35u,950u,
    30u,1000u, 26u,1050u, 22u,1100u, 19u,1150u, 16u,1200u,
    14u,1250u, 12u,1300u, 11u,1350u, 9u,1400u, 8u,1450u
};

static uint16_t level_value(uint8_t level, uint16_t first,
                            uint16_t second, uint16_t third)
{
    return (level == 0u) ? first : ((level == 1u) ? second : third);
}

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

static uint8_t filter_update(sh3510_filter_t *f, uint16_t value,
                             uint16_t trip, uint16_t recover,
                             uint16_t filter_10ms, uint8_t high)
{
    uint8_t violated;
    uint8_t recovered;
    uint16_t needed;
    if (f == 0) return 0u;
    if (trip == 0u) {
        f->active = 0u; f->trip_count = 0u; f->recover_count = 0u;
        return 0u;
    }

    needed = filter_samples(filter_10ms);
    if (f->active) {
        recovered = high ? (value <= recover) : (value >= recover);
        if (recovered) {
            if (f->recover_count < needed) ++f->recover_count;
            if (f->recover_count >= needed) {
                f->active = 0u;
                f->trip_count = 0u;
                f->recover_count = 0u;
            }
        } else {
            f->recover_count = 0u;
        }
        return f->active;
    }

    violated = high ? (value >= trip) : (value <= trip);
    if (violated) {
        if (f->trip_count < needed) ++f->trip_count;
        if (f->trip_count >= needed) {
            f->active = 1u;
            f->trip_count = 0u;
            f->recover_count = 0u;
        }
    } else if (f->trip_count != 0u) {
        --f->trip_count;
    }
    return f->active;
}

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
    s_comm_failures = 0u;
}

static union MDLCHGFAULT_REG *fault_reg(uint8_t level)
{
    if (level == 0u) return &g_stCellInfoReport.unMdlFault_First;
    if (level == 1u) return &g_stCellInfoReport.unMdlFault_Second;
    return &g_stCellInfoReport.unMdlFault_Third;
}

static void record_rising(uint8_t level, union MDLCHGFAULT_REG now)
{
    union MDLCHGFAULT_REG prev = s_prev_fault[level];
    uint8_t base = (uint8_t)(1u + 13u * level);
#define RISE(bit, off) do { if (now.bits.bit && !prev.bits.bit) \
    bms_fault_history_record((bms_fault_code_t)(base + (off))); } while (0)
    RISE(b1CellOvp,0u); RISE(b1CellUvp,1u); RISE(b1BatOvp,2u);
    RISE(b1BatUvp,3u); RISE(b1IchgOcp,4u); RISE(b1IdischgOcp,5u);
    RISE(b1CellChgOtp,6u); RISE(b1CellChgUtp,7u);
    RISE(b1CellDischgOtp,8u); RISE(b1CellDischgUtp,9u);
    RISE(b1TmosOtp,10u); RISE(b1VcellDeltaBig,11u);
#undef RISE
    s_prev_fault[level] = now;
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

static void update_faults(void)
{
    uint8_t l;
    uint16_t bat_min = 0u, bat_max = 0u, mos_temp = 0u;
    uint8_t temp_ok = temperature_snapshot(&bat_min, &bat_max, &mos_temp);

    if (temp_ok) bms_error_clear(BMS_ERROR_TEMP_BREAK);
    else if (!bms_error_get(BMS_ERROR_TEMP_BREAK)) bms_error_raise(BMS_ERROR_TEMP_BREAK);

    for (l = 0u; l < SH3510_LEVEL_COUNT; ++l) {
        union MDLCHGFAULT_REG *f = fault_reg(l);
        uint16_t trip;

        trip = level_value(l, g_tParam.protect.u16VcellOvp_First,
                           g_tParam.protect.u16VcellOvp_Second,
                           g_tParam.protect.u16VcellOvp_Third);
        f->bits.b1CellOvp = filter_update(&s_filter[l][F_CELL_OV],
            g_stCellInfoReport.u16VCellMax, trip, g_tParam.protect.u16VcellOvp_Rcv,
            g_tParam.protect.u16VcellOvp_Filter, 1u);
        trip = level_value(l, g_tParam.protect.u16VcellUvp_First,
                           g_tParam.protect.u16VcellUvp_Second,
                           g_tParam.protect.u16VcellUvp_Third);
        f->bits.b1CellUvp = filter_update(&s_filter[l][F_CELL_UV],
            g_stCellInfoReport.u16VCellMin, trip, g_tParam.protect.u16VcellUvp_Rcv,
            g_tParam.protect.u16VcellUvp_Filter, 0u);

        trip = level_value(l, g_tParam.protect.u16VbusOvp_First,
                           g_tParam.protect.u16VbusOvp_Second,
                           g_tParam.protect.u16VbusOvp_Third);
        f->bits.b1BatOvp = filter_update(&s_filter[l][F_PACK_OV],
            g_stCellInfoReport.u16VCellTotle, trip, g_tParam.protect.u16VbusOvp_Rcv,
            g_tParam.protect.u16VbusOvp_Filter, 1u);
        trip = level_value(l, g_tParam.protect.u16VbusUvp_First,
                           g_tParam.protect.u16VbusUvp_Second,
                           g_tParam.protect.u16VbusUvp_Third);
        f->bits.b1BatUvp = filter_update(&s_filter[l][F_PACK_UV],
            g_stCellInfoReport.u16VCellTotle, trip, g_tParam.protect.u16VbusUvp_Rcv,
            g_tParam.protect.u16VbusUvp_Filter, 0u);

        trip = level_value(l, g_tParam.protect.u16IchgOcp_First,
                           g_tParam.protect.u16IchgOcp_Second,
                           g_tParam.protect.u16IchgOcp_Third);
        f->bits.b1IchgOcp = filter_update(&s_filter[l][F_CHG_OC],
            g_stCellInfoReport.u16Ichg, trip, g_tParam.protect.u16IchgOcp_Rcv,
            g_tParam.protect.u16IchgOcp_Filter, 1u);
        trip = level_value(l, g_tParam.protect.u16IdsgOcp_First,
                           g_tParam.protect.u16IdsgOcp_Second,
                           g_tParam.protect.u16IdsgOcp_Third);
        f->bits.b1IdischgOcp = filter_update(&s_filter[l][F_DSG_OC],
            g_stCellInfoReport.u16IDischg, trip, g_tParam.protect.u16IdsgOcp_Rcv,
            g_tParam.protect.u16IdsgOcp_Filter, 1u);

        if (temp_ok) {
            trip = level_value(l, g_tParam.protect.u16TChgOTp_First,
                               g_tParam.protect.u16TChgOTp_Second,
                               g_tParam.protect.u16TChgOTp_Third);
            f->bits.b1CellChgOtp = filter_update(&s_filter[l][F_CHG_OT], bat_max,
                trip, g_tParam.protect.u16TChgOTp_Rcv,
                g_tParam.protect.u16TChgOTp_Filter, 1u);
            trip = level_value(l, g_tParam.protect.u16TchgUTp_First,
                               g_tParam.protect.u16TchgUTp_Second,
                               g_tParam.protect.u16TchgUTp_Third);
            f->bits.b1CellChgUtp = filter_update(&s_filter[l][F_CHG_UT], bat_min,
                trip, g_tParam.protect.u16TchgUTp_Rcv,
                g_tParam.protect.u16TchgUTp_Filter, 0u);
            trip = level_value(l, g_tParam.protect.u16TdischgOTp_First,
                               g_tParam.protect.u16TdischgOTp_Second,
                               g_tParam.protect.u16TdischgOTp_Third);
            f->bits.b1CellDischgOtp = filter_update(&s_filter[l][F_DSG_OT], bat_max,
                trip, g_tParam.protect.u16TdischgOTp_Rcv,
                g_tParam.protect.u16TdischgOTp_Filter, 1u);
            trip = level_value(l, g_tParam.protect.u16TdischgUTp_First,
                               g_tParam.protect.u16TdischgUTp_Second,
                               g_tParam.protect.u16TdischgUTp_Third);
            f->bits.b1CellDischgUtp = filter_update(&s_filter[l][F_DSG_UT], bat_min,
                trip, g_tParam.protect.u16TdischgUTp_Rcv,
                g_tParam.protect.u16TdischgUTp_Filter, 0u);
            trip = level_value(l, g_tParam.protect.u16TmosOTp_First,
                               g_tParam.protect.u16TmosOTp_Second,
                               g_tParam.protect.u16TmosOTp_Third);
            f->bits.b1TmosOtp = filter_update(&s_filter[l][F_MOS_OT], mos_temp,
                trip, g_tParam.protect.u16TmosOTp_Rcv,
                g_tParam.protect.u16TmosOTp_Filter, 1u);
        } else {
            f->bits.b1CellChgOtp = 0u; f->bits.b1CellChgUtp = 0u;
            f->bits.b1CellDischgOtp = 0u; f->bits.b1CellDischgUtp = 0u;
            f->bits.b1TmosOtp = 0u;
        }

        trip = level_value(l, g_tParam.protect.u16VdeltaOvp_First,
                           g_tParam.protect.u16VdeltaOvp_Second,
                           g_tParam.protect.u16VdeltaOvp_Third);
        f->bits.b1VcellDeltaBig = filter_update(&s_filter[l][F_VDELTA],
            g_stCellInfoReport.u16VCellDelta, trip,
            g_tParam.protect.u16VdeltaOvp_Rcv,
            g_tParam.protect.u16VdeltaOvp_Filter, 1u);
        record_rising(l, *f);
    }
}

static uint8_t charge_blocked(void)
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

static uint8_t sh3510_outputs_healthy(void)
{
    return (s_snapshot_valid && !s_output_inhibit && !s_hw_afe_error &&
            !bms_error_get(BMS_ERROR_AFE1) && !bms_error_get(BMS_ERROR_SPI)) ? 1u : 0u;
}

static uint8_t sh3510_apply_requested_fets(void)
{
    uint8_t charge_on = 0u;
    uint8_t discharge_on = 0u;
    uint8_t key_on;

    if (!sh3673510_control_ready()) return 0u;
    key_on = gpio_read(D011_SWITCH_PIN) ? 0u : 1u;

    if (s_output_enabled && sh3510_outputs_healthy()) {
        charge_on = s_requested_charge_on ? 1u : 0u;
        discharge_on = (s_requested_discharge_on && key_on) ? 1u : 0u;
        if (charge_blocked()) charge_on = 0u;
        if (discharge_blocked()) discharge_on = 0u;
    }

    if (!sh3673510_control_set_fets(charge_on, discharge_on)) {
        note_comm_error();
        return 0u;
    }
    return 1u;
}

static void apply_heater(void)
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

static void apply_balance(void)
{
    uint16_t threshold = g_tParam.protect.u16VdeltaOvp_First;
    uint16_t mask = 0u;
    uint8_t i;
    if (s_output_enabled && threshold && g_stCellInfoReport.u16Ichg &&
        g_stCellInfoReport.u16VCellDelta >= threshold && !charge_blocked()) {
        for (i = 0u; i < SH3673510_D011_CELL_COUNT; ++i) {
            if ((uint16_t)(g_stCellInfoReport.u16VCell[i] -
                           g_stCellInfoReport.u16VCellMin) >= threshold)
                mask |= (uint16_t)(1u << i);
        }
    }
    /* SH36735xx balance bits time out, so refresh a nonzero mask every sample. */
    if (mask != s_balance_mask || mask != 0u) {
        if (sh3673510_control_set_balance(mask)) bms_error_clear(BMS_ERROR_BALANCE);
        else {
            if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
            mask = 0u;
        }
        s_balance_mask = mask;
    }
    g_stCellInfoReport.u16BalanceFlag1 = s_balance_mask;
    g_stCellInfoReport.u16BalanceFlag2 = 0u;
    g_bms_system_status.bits.b1Status_Balance = s_balance_mask ? 1u : 0u;
}

static void publish_hw_status(const sh3673510_control_status_t *s)
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
        if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
            s_short_clear_pending = 0u;
        }
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
    uint8_t actual_ok;
    uint8_t c1 = 0u, c2 = 0u;
    uint16_t bat_min = 0u, bat_max = 0u;
    uint8_t bat_temp_ok;
    if (s == 0) return;

    actual_ok = sh3673510_control_get_protection_actual(&actual);

    /* Reset/wake events are diagnostic. RST1/RST2 are intentionally NOT
     * cleared here: service_afe_reconfiguration() owns those states. */
    c1 |= (uint8_t)(s->flag1 & SH3673520_FLAG1_WK_MASK);
    if (s->flag2 & SH3673520_FLAG2_WDT_MASK) c2 |= SH3673520_FLAG2_WDT_MASK;

    if (s->flag1 & SH3673520_FLAG1_OV_MASK) {
        if (hw_recovery_stable(HW_REC_OV,
                actual_ok &&
                g_stCellInfoReport.u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv &&
                g_stCellInfoReport.u16VCellMax < actual.ov_mv,
                g_tParam.protect.u16VcellOvp_Filter)) c1 |= SH3673520_FLAG1_OV_MASK;
    } else s_hw_recovery_count[HW_REC_OV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_UV_MASK) {
        if (hw_recovery_stable(HW_REC_UV,
                actual_ok &&
                g_stCellInfoReport.u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv &&
                g_stCellInfoReport.u16VCellMin > actual.uv_mv,
                g_tParam.protect.u16VcellUvp_Filter)) c1 |= SH3673520_FLAG1_UV_MASK;
    } else s_hw_recovery_count[HW_REC_UV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD1_MASK) {
        if (hw_recovery_stable(HW_REC_OCD1,
                actual_ok &&
                g_stCellInfoReport.u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv &&
                g_stCellInfoReport.u16IDischg < actual.ocd1_a10,
                g_tParam.protect.u16IdsgOcp_Filter)) c1 |= SH3673520_FLAG1_OCD1_MASK;
    } else s_hw_recovery_count[HW_REC_OCD1] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD2_MASK) {
        if (hw_recovery_stable(HW_REC_OCD2,
                actual_ok &&
                g_stCellInfoReport.u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv &&
                g_stCellInfoReport.u16IDischg < actual.ocd2_a10,
                g_tParam.protect.u16IdsgOcp_Filter)) c1 |= SH3673520_FLAG1_OCD2_MASK;
    } else s_hw_recovery_count[HW_REC_OCD2] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCC_MASK) {
        if (hw_recovery_stable(HW_REC_OCC,
                actual_ok &&
                g_stCellInfoReport.u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv &&
                g_stCellInfoReport.u16Ichg < actual.occ_a10,
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

static uint8_t publish_measurements(void)
{
    int32_t cell[SH3673510_D011_CELL_COUNT];
    int32_t pack_mv;
    int32_t current_ma;
    sh3673520_current_raw_t current;
    sh3673520_temperature_raw_t temp;
    sh3673510_control_status_t status;
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

    if (current_ma < 0L) {
        uint32_t ma = (uint32_t)(-current_ma);
        g_stCellInfoReport.u16Ichg = (uint16_t)((ma + 50u) / 100u);
        g_stCellInfoReport.u16IDischg = 0u;
    } else {
        uint32_t ma = (uint32_t)current_ma;
        g_stCellInfoReport.u16IDischg = (uint16_t)((ma + 50u) / 100u);
        g_stCellInfoReport.u16Ichg = 0u;
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
    g_stCellInfoReport.u16Temperature[AFE1_TEMP3] =
        s_ntc_valid[SH3673510_D011_HEATER_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_HEATER_NTC_INDEX]) : 0u;
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] =
        s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_D011_MOS_NTC_INDEX]) : 0u;

    /*
     * Realtime max/min temperature is the battery temperature range: TS1/TS2.
     * TS3 supervises the heater MOS and TS4 supervises the power MOS, so they
     * must not be folded into the battery extrema. Zero is the existing
     * invalid/sensor-break sentinel (-40.0 C in the legacy encoding).
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
    s_aux.mos_ntc_100ohm = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ?
        s_ntc_ohm[SH3673510_D011_MOS_NTC_INDEX] / 100u : 0u;
    s_aux.battery_ntc_mv = legacy_adc_mv(s_aux.battery_ntc_100ohm * 100u);
    s_aux.mos_ntc_mv = legacy_adc_mv(s_aux.mos_ntc_100ohm * 100u);

    publish_hw_status(&status);
    if (!s_afe_reconfigure_required) {
        update_faults();
        service_short_recovery(&status);
        service_hw_flag_recovery(&status);
    }
    return 1u;
}

void sh3673510_bms_afe_init(void)
{
    memset(s_filter, 0, sizeof(s_filter));
    memset(s_prev_fault, 0, sizeof(s_prev_fault));
    memset(&s_aux, 0, sizeof(s_aux));
    s_snapshot_valid = 0u;
    s_comm_failures = 0u;
    s_reinit_cooldown = 0u;
    s_heater_on = 0u;
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
    s_heater_mos_overtemp = 0u;
    sh3673510_board_force_heater_fuse_safe();
    sh3673510_board_set_heater(0u);
    if (!sh3673510_control_init()) { note_comm_error(); return; }
    note_comm_ok();
}

void sh3673510_bms_afe_sample(void)
{
    sh3673510_board_force_heater_fuse_safe();
    if (s_reinit_cooldown) --s_reinit_cooldown;
    if (!publish_measurements()) {
        s_snapshot_valid = 0u;
                s_heater_on = 0u;
        sh3673510_board_set_heater(0u);
        g_bms_system_status.bits.b1Status_Heat = 0u;
        (void)sh3673510_control_set_balance(0u);
        s_balance_mask = 0u;
        (void)sh3673510_control_set_fets(0u, 0u);
        note_comm_error();
        if (s_comm_failures != 0xFFu) ++s_comm_failures;
        if (s_comm_failures >= SH3510_REINIT_TRIGGER && s_reinit_cooldown == 0u) {
            s_reinit_cooldown = SH3510_REINIT_COOLDOWN;
            if (sh3673510_control_init()) {
                memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
                s_hw_charge_protect = 0u;
                s_hw_discharge_protect = 0u;
                note_comm_ok();
            }
        }
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
    apply_heater();
    apply_balance();
    (void)sh3510_apply_requested_fets();
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
        s_heater_on = 0u;
        sh3673510_board_set_heater(0u);
        if (sh3673510_control_ready()) {
            (void)sh3673510_control_set_balance(0u);
            (void)sh3673510_control_set_fets(0u, 0u);
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

void sh3673510_bms_afe_sleep(void)
{
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_heater_on = 0u;
    sh3673510_board_force_heater_fuse_safe();
    sh3673510_board_set_heater(0u);
    s_balance_mask = 0u;
    (void)sh3673510_control_set_balance(0u);
    sh3673510_control_sleep();
}
