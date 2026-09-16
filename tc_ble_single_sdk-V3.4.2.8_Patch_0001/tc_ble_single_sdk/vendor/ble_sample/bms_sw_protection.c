#include "bms_sw_protection.h"

#include "bms_error.h"
#include "bms_state.h"
#include "param.h"
#include <string.h>

/*
 * Unified software-protection policy shared by D008 / D011 / D013.
 *
 * Scope:
 *   - software threshold/filter/recovery state only;
 *   - no AFE register access, GPIO access or hardware-latch clearing;
 *   - Level 1/2 are report/alarm levels;
 *   - Level 3 is the software MOS-blocking level;
 *   - AFE hardware protection remains an independent backup. Backends merge
 *     hardware flags into Third after this module evaluates the software state.
 *
 * u16SocUp_* / b1SocLow are intentionally excluded from v1 because the legacy
 * naming/semantics are inconsistent. They must be specified before being made
 * part of the common protection state machine.
 */
#define BMS_SW_PROTECTION_SAMPLE_MS       200u
#define BMS_SW_PROTECTION_LEVEL_COUNT     3u
#define BMS_SW_PROTECTION_FILTER_COUNT    12u

typedef struct
{
    uint16_t trip_count;
    uint16_t recover_count;
    uint8_t active;
} bms_sw_filter_t;

typedef enum
{
    BMS_SW_F_CELL_OV = 0,
    BMS_SW_F_CELL_UV,
    BMS_SW_F_PACK_OV,
    BMS_SW_F_PACK_UV,
    BMS_SW_F_CHG_OC,
    BMS_SW_F_DSG_OC,
    BMS_SW_F_CHG_OT,
    BMS_SW_F_CHG_UT,
    BMS_SW_F_DSG_OT,
    BMS_SW_F_DSG_UT,
    BMS_SW_F_MOS_OT,
    BMS_SW_F_VDELTA,
    BMS_SW_F_COUNT
} bms_sw_filter_id_t;

typedef enum
{
    BMS_SW_HIGH = 0,
    BMS_SW_LOW
} bms_sw_direction_t;

static bms_sw_filter_t s_filter[BMS_SW_PROTECTION_LEVEL_COUNT][BMS_SW_F_COUNT];
static bms_fault_reg_t s_prev_fault[BMS_SW_PROTECTION_LEVEL_COUNT];

static uint8_t bms_sw_high_recovery_valid(uint16_t first,
                                          uint16_t second,
                                          uint16_t third,
                                          uint16_t recover)
{
    return !((first && recover >= first) ||
             (second && recover >= second) ||
             (third && recover >= third));
}

static uint8_t bms_sw_low_recovery_valid(uint16_t first,
                                         uint16_t second,
                                         uint16_t third,
                                         uint16_t recover)
{
    return !((first && recover <= first) ||
             (second && recover <= second) ||
             (third && recover <= third));
}

static uint16_t bms_sw_level_value(uint8_t level,
                                   uint16_t first,
                                   uint16_t second,
                                   uint16_t third)
{
    return (level == 0u) ? first : ((level == 1u) ? second : third);
}

static uint16_t bms_sw_filter_samples(uint16_t filter_10ms)
{
    uint32_t delay_ms = (uint32_t)filter_10ms * 10u;
    uint32_t samples;

    if (delay_ms == 0u) return 1u;
    samples = (delay_ms + BMS_SW_PROTECTION_SAMPLE_MS - 1u) /
              BMS_SW_PROTECTION_SAMPLE_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

static void bms_sw_filter_reset(bms_sw_filter_t *state)
{
    if (state == 0) return;
    state->trip_count = 0u;
    state->recover_count = 0u;
    state->active = 0u;
}

static uint8_t bms_sw_filter_update(bms_sw_filter_t *state,
                                    uint16_t value,
                                    uint16_t trip,
                                    uint16_t recover,
                                    uint16_t filter_10ms,
                                    bms_sw_direction_t direction)
{
    uint16_t required;
    uint8_t violated;
    uint8_t recovered;

    if (state == 0) return 0u;
    if (trip == 0u)
    {
        bms_sw_filter_reset(state);
        return 0u;
    }

    required = bms_sw_filter_samples(filter_10ms);
    if (state->active)
    {
        recovered = (direction == BMS_SW_HIGH) ?
                    (value <= recover) : (value >= recover);
        if (recovered)
        {
            if (state->recover_count < required) ++state->recover_count;
            if (state->recover_count >= required)
                bms_sw_filter_reset(state);
        }
        else
        {
            state->recover_count = 0u;
        }
        return state->active;
    }

    violated = (direction == BMS_SW_HIGH) ?
               (value >= trip) : (value <= trip);
    if (violated)
    {
        if (state->trip_count < required) ++state->trip_count;
        if (state->trip_count >= required)
        {
            state->active = 1u;
            state->trip_count = 0u;
            state->recover_count = 0u;
        }
    }
    else if (state->trip_count != 0u)
    {
        /* Preserve the existing D011/D013 leaky debounce behavior instead of
         * resetting the trip accumulator on one clean sample. */
        --state->trip_count;
    }
    return state->active;
}

/*
 * Battery-temperature faults are directional: a charge OTP/UTP can start only
 * while charge current exists, and a discharge OTP/UTP can start only while
 * discharge current exists.
 *
 * The current gate is intentionally applied only while qualifying a NEW fault.
 * Once active, protection usually removes that current; clearing the fault just
 * because current became zero would immediately re-open the FET and create an
 * on/off loop. Active faults therefore recover only from temperature + recovery
 * filter, independent of current after the trip.
 */
static uint8_t bms_sw_temp_filter_update(bms_sw_filter_t *state,
                                         uint8_t trip_enabled,
                                         uint16_t value,
                                         uint16_t trip,
                                         uint16_t recover,
                                         uint16_t filter_10ms,
                                         bms_sw_direction_t direction)
{
    if (state == 0) return 0u;

    if (!state->active && !trip_enabled)
    {
        /* Temperature and matching current direction must coexist throughout
         * qualification. Never carry a partial trip count through idle/current
         * reversal periods. */
        state->trip_count = 0u;
        state->recover_count = 0u;
        return 0u;
    }

    return bms_sw_filter_update(state, value, trip, recover,
                                filter_10ms, direction);
}

static bms_fault_reg_t *bms_sw_fault_reg(uint8_t level)
{
    if (level == 0u) return &g_stCellInfoReport.unMdlFault_First;
    if (level == 1u) return &g_stCellInfoReport.unMdlFault_Second;
    return &g_stCellInfoReport.unMdlFault_Third;
}

static void bms_sw_clear_managed_bits(bms_fault_reg_t *fault)
{
    if (fault == 0) return;
    fault->bits.b1CellOvp = 0u;
    fault->bits.b1CellUvp = 0u;
    fault->bits.b1BatOvp = 0u;
    fault->bits.b1BatUvp = 0u;
    fault->bits.b1IchgOcp = 0u;
    fault->bits.b1IdischgOcp = 0u;
    fault->bits.b1CellChgOtp = 0u;
    fault->bits.b1CellChgUtp = 0u;
    fault->bits.b1CellDischgOtp = 0u;
    fault->bits.b1CellDischgUtp = 0u;
    fault->bits.b1TmosOtp = 0u;
    fault->bits.b1VcellDeltaBig = 0u;
}

uint8_t bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *p)
{
    if (p == 0) return 0u;
    if ((p->u16VcellOvp_First > p->u16VcellOvp_Second) ||
        (p->u16VcellOvp_Second > p->u16VcellOvp_Third) ||
        (p->u16VbusOvp_First > p->u16VbusOvp_Second) ||
        (p->u16VbusOvp_Second > p->u16VbusOvp_Third) ||
        (p->u16IchgOcp_First > p->u16IchgOcp_Second) ||
        (p->u16IchgOcp_Second > p->u16IchgOcp_Third) ||
        (p->u16IdsgOcp_First > p->u16IdsgOcp_Second) ||
        (p->u16IdsgOcp_Second > p->u16IdsgOcp_Third) ||
        (p->u16TChgOTp_First > p->u16TChgOTp_Second) ||
        (p->u16TChgOTp_Second > p->u16TChgOTp_Third) ||
        (p->u16TdischgOTp_First > p->u16TdischgOTp_Second) ||
        (p->u16TdischgOTp_Second > p->u16TdischgOTp_Third) ||
        (p->u16TmosOTp_First > p->u16TmosOTp_Second) ||
        (p->u16TmosOTp_Second > p->u16TmosOTp_Third) ||
        (p->u16VdeltaOvp_First > p->u16VdeltaOvp_Second) ||
        (p->u16VdeltaOvp_Second > p->u16VdeltaOvp_Third)) return 0u;
    if ((p->u16VcellUvp_First < p->u16VcellUvp_Second) ||
        (p->u16VcellUvp_Second < p->u16VcellUvp_Third) ||
        (p->u16VbusUvp_First < p->u16VbusUvp_Second) ||
        (p->u16VbusUvp_Second < p->u16VbusUvp_Third) ||
        (p->u16TchgUTp_First < p->u16TchgUTp_Second) ||
        (p->u16TchgUTp_Second < p->u16TchgUTp_Third) ||
        (p->u16TdischgUTp_First < p->u16TdischgUTp_Second) ||
        (p->u16TdischgUTp_Second < p->u16TdischgUTp_Third)) return 0u;
    if (!bms_sw_high_recovery_valid(p->u16VcellOvp_First, p->u16VcellOvp_Second,
                                    p->u16VcellOvp_Third, p->u16VcellOvp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16VbusOvp_First, p->u16VbusOvp_Second,
                                    p->u16VbusOvp_Third, p->u16VbusOvp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16IchgOcp_First, p->u16IchgOcp_Second,
                                    p->u16IchgOcp_Third, p->u16IchgOcp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16IdsgOcp_First, p->u16IdsgOcp_Second,
                                    p->u16IdsgOcp_Third, p->u16IdsgOcp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16TChgOTp_First, p->u16TChgOTp_Second,
                                    p->u16TChgOTp_Third, p->u16TChgOTp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16TdischgOTp_First, p->u16TdischgOTp_Second,
                                    p->u16TdischgOTp_Third, p->u16TdischgOTp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16TmosOTp_First, p->u16TmosOTp_Second,
                                    p->u16TmosOTp_Third, p->u16TmosOTp_Rcv) ||
        !bms_sw_high_recovery_valid(p->u16VdeltaOvp_First, p->u16VdeltaOvp_Second,
                                    p->u16VdeltaOvp_Third, p->u16VdeltaOvp_Rcv)) return 0u;
    if (!bms_sw_low_recovery_valid(p->u16VcellUvp_First, p->u16VcellUvp_Second,
                                   p->u16VcellUvp_Third, p->u16VcellUvp_Rcv) ||
        !bms_sw_low_recovery_valid(p->u16VbusUvp_First, p->u16VbusUvp_Second,
                                   p->u16VbusUvp_Third, p->u16VbusUvp_Rcv) ||
        !bms_sw_low_recovery_valid(p->u16TchgUTp_First, p->u16TchgUTp_Second,
                                   p->u16TchgUTp_Third, p->u16TchgUTp_Rcv) ||
        !bms_sw_low_recovery_valid(p->u16TdischgUTp_First, p->u16TdischgUTp_Second,
                                   p->u16TdischgUTp_Third, p->u16TdischgUTp_Rcv)) return 0u;
    if (p->u16TChgOTp_Third > 1450u || p->u16TChgOTp_Rcv > 1450u ||
        p->u16TchgUTp_Third > 1450u || p->u16TchgUTp_Rcv > 1450u ||
        p->u16TdischgOTp_Third > 1450u || p->u16TdischgOTp_Rcv > 1450u ||
        p->u16TdischgUTp_Third > 1450u || p->u16TdischgUTp_Rcv > 1450u ||
        p->u16TmosOTp_Third > 1450u || p->u16TmosOTp_Rcv > 1450u) return 0u;
    return 1u;
}

void bms_sw_protection_clear(void)
{
    uint8_t level;
    memset(s_filter, 0, sizeof(s_filter));
    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
        bms_sw_clear_managed_bits(bms_sw_fault_reg(level));
    bms_error_clear(BMS_ERROR_TEMP_BREAK);
}

void bms_sw_protection_init(void)
{
    memset(s_prev_fault, 0, sizeof(s_prev_fault));
    bms_sw_protection_clear();
}

void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs)
{
    const struct PRT_E2ROM_PARAS *p = &g_tParam.protect;
    uint8_t level;
    uint8_t charge_current_present;
    uint8_t discharge_current_present;

    if (inputs == 0) return;
    if (!bms_protection_params_valid())
    {
        bms_sw_protection_clear();
        return;
    }

    charge_current_present = (g_stCellInfoReport.u16Ichg > 0u) ? 1u : 0u;
    discharge_current_present = (g_stCellInfoReport.u16IDischg > 0u) ? 1u : 0u;

    /* Sensor-break handling remains fail-safe at the system level, but each
     * temperature protection group is evaluated only from the sensor it owns.
     * A missing MOS NTC must not erase battery OTP/UTP state, and vice versa. */
    if (inputs->battery_temp_valid && inputs->mos_temp_valid)
        bms_error_clear(BMS_ERROR_TEMP_BREAK);
    else
        bms_error_raise(BMS_ERROR_TEMP_BREAK);

    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
    {
        bms_fault_reg_t *f = bms_sw_fault_reg(level);
        uint16_t trip;

        trip = bms_sw_level_value(level, p->u16VcellOvp_First,
                                  p->u16VcellOvp_Second, p->u16VcellOvp_Third);
        f->bits.b1CellOvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CELL_OV],
            g_stCellInfoReport.u16VCellMax, trip, p->u16VcellOvp_Rcv,
            p->u16VcellOvp_Filter, BMS_SW_HIGH);

        trip = bms_sw_level_value(level, p->u16VcellUvp_First,
                                  p->u16VcellUvp_Second, p->u16VcellUvp_Third);
        f->bits.b1CellUvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CELL_UV],
            g_stCellInfoReport.u16VCellMin, trip, p->u16VcellUvp_Rcv,
            p->u16VcellUvp_Filter, BMS_SW_LOW);

        trip = bms_sw_level_value(level, p->u16VbusOvp_First,
                                  p->u16VbusOvp_Second, p->u16VbusOvp_Third);
        f->bits.b1BatOvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_PACK_OV],
            g_stCellInfoReport.u16VCellTotle, trip, p->u16VbusOvp_Rcv,
            p->u16VbusOvp_Filter, BMS_SW_HIGH);

        trip = bms_sw_level_value(level, p->u16VbusUvp_First,
                                  p->u16VbusUvp_Second, p->u16VbusUvp_Third);
        f->bits.b1BatUvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_PACK_UV],
            g_stCellInfoReport.u16VCellTotle, trip, p->u16VbusUvp_Rcv,
            p->u16VbusUvp_Filter, BMS_SW_LOW);

        trip = bms_sw_level_value(level, p->u16IchgOcp_First,
                                  p->u16IchgOcp_Second, p->u16IchgOcp_Third);
        f->bits.b1IchgOcp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CHG_OC],
            g_stCellInfoReport.u16Ichg, trip, p->u16IchgOcp_Rcv,
            p->u16IchgOcp_Filter, BMS_SW_HIGH);

        trip = bms_sw_level_value(level, p->u16IdsgOcp_First,
                                  p->u16IdsgOcp_Second, p->u16IdsgOcp_Third);
        f->bits.b1IdischgOcp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_DSG_OC],
            g_stCellInfoReport.u16IDischg, trip, p->u16IdsgOcp_Rcv,
            p->u16IdsgOcp_Filter, BMS_SW_HIGH);

        if (inputs->battery_temp_valid)
        {
            trip = bms_sw_level_value(level, p->u16TChgOTp_First,
                                      p->u16TChgOTp_Second, p->u16TChgOTp_Third);
            f->bits.b1CellChgOtp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_CHG_OT],
                charge_current_present, inputs->battery_temp_max, trip, p->u16TChgOTp_Rcv,
                p->u16TChgOTp_Filter, BMS_SW_HIGH);

            trip = bms_sw_level_value(level, p->u16TchgUTp_First,
                                      p->u16TchgUTp_Second, p->u16TchgUTp_Third);
            f->bits.b1CellChgUtp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_CHG_UT],
                charge_current_present, inputs->battery_temp_min, trip, p->u16TchgUTp_Rcv,
                p->u16TchgUTp_Filter, BMS_SW_LOW);

            trip = bms_sw_level_value(level, p->u16TdischgOTp_First,
                                      p->u16TdischgOTp_Second, p->u16TdischgOTp_Third);
            f->bits.b1CellDischgOtp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_DSG_OT],
                discharge_current_present, inputs->battery_temp_max, trip, p->u16TdischgOTp_Rcv,
                p->u16TdischgOTp_Filter, BMS_SW_HIGH);

            trip = bms_sw_level_value(level, p->u16TdischgUTp_First,
                                      p->u16TdischgUTp_Second, p->u16TdischgUTp_Third);
            f->bits.b1CellDischgUtp = bms_sw_temp_filter_update(&s_filter[level][BMS_SW_F_DSG_UT],
                discharge_current_present, inputs->battery_temp_min, trip, p->u16TdischgUTp_Rcv,
                p->u16TdischgUTp_Filter, BMS_SW_LOW);
        }
        else
        {
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_OT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_UT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_DSG_OT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_DSG_UT]);
            f->bits.b1CellChgOtp = 0u;
            f->bits.b1CellChgUtp = 0u;
            f->bits.b1CellDischgOtp = 0u;
            f->bits.b1CellDischgUtp = 0u;
        }

        if (inputs->mos_temp_valid)
        {
            trip = bms_sw_level_value(level, p->u16TmosOTp_First,
                                      p->u16TmosOTp_Second, p->u16TmosOTp_Third);
            f->bits.b1TmosOtp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_MOS_OT],
                inputs->mos_temp, trip, p->u16TmosOTp_Rcv,
                p->u16TmosOTp_Filter, BMS_SW_HIGH);
        }
        else
        {
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_MOS_OT]);
            f->bits.b1TmosOtp = 0u;
        }

        trip = bms_sw_level_value(level, p->u16VdeltaOvp_First,
                                  p->u16VdeltaOvp_Second, p->u16VdeltaOvp_Third);
        f->bits.b1VcellDeltaBig = bms_sw_filter_update(&s_filter[level][BMS_SW_F_VDELTA],
            g_stCellInfoReport.u16VCellDelta, trip, p->u16VdeltaOvp_Rcv,
            p->u16VdeltaOvp_Filter, BMS_SW_HIGH);
    }
}

void bms_sw_protection_record_fault_edges(void)
{
    uint8_t level;
    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
    {
        bms_fault_reg_t *now = bms_sw_fault_reg(level);
        bms_fault_reg_t *prev = &s_prev_fault[level];
        uint8_t base = (uint8_t)(1u + 13u * level);
#define BMS_SW_RISE(field, offset) \
        do { if (now->bits.field && !prev->bits.field) \
            bms_fault_history_record((bms_fault_code_t)(base + (offset))); } while (0)
        BMS_SW_RISE(b1CellOvp, 0u);
        BMS_SW_RISE(b1CellUvp, 1u);
        BMS_SW_RISE(b1BatOvp, 2u);
        BMS_SW_RISE(b1BatUvp, 3u);
        BMS_SW_RISE(b1IchgOcp, 4u);
        BMS_SW_RISE(b1IdischgOcp, 5u);
        BMS_SW_RISE(b1CellChgOtp, 6u);
        BMS_SW_RISE(b1CellChgUtp, 7u);
        BMS_SW_RISE(b1CellDischgOtp, 8u);
        BMS_SW_RISE(b1CellDischgUtp, 9u);
        BMS_SW_RISE(b1TmosOtp, 10u);
        BMS_SW_RISE(b1VcellDeltaBig, 11u);
#undef BMS_SW_RISE
        *prev = *now;
    }
}

uint8_t bms_sw_protection_charge_blocked(void)
{
    const bms_fault_bits_t *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

uint8_t bms_sw_protection_discharge_blocked(void)
{
    const bms_fault_bits_t *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}
