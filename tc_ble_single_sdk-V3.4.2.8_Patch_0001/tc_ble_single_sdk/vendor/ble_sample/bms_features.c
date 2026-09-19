#include "bms_diag.h"
#include "bms_features.h"
#include "bms_config_store.h"

#include "bms_board.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "param.h"
#include <string.h>

#define BMS_OPENWIRE_FIRST_IDLE_SAMPLES \
    ((BMS_OPENWIRE_FIRST_IDLE_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)
#define BMS_OPENWIRE_PERIOD_SAMPLES \
    ((BMS_OPENWIRE_PERIOD_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)
#define BMS_BALANCE_TRUST_CONFIRM_SAMPLES \
    ((BMS_BALANCE_TRUST_CONFIRM_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)

typedef struct {
    uint8_t heater_on;
    uint8_t heater_overtemp_latched;
    bms_heater_state_t heater_state;
    uint8_t charge_session_active;

    uint8_t openwire_active;
    uint8_t openwire_fault_latched;
    uint8_t openwire_suspected;
    uint16_t openwire_idle_samples;
    uint16_t openwire_cooldown_samples;
    bms_afe_openwire_result_t openwire_result;

    uint8_t balance_active;
    uint8_t balance_voltage_trusted;
    uint16_t balance_trust_samples;
    uint8_t balance_prev_cell_count;
    uint16_t balance_prev_cell_mv[BMS_AFE_FEATURE_MAX_CELLS];
    uint32_t balance_requested_mask;
} bms_feature_state_t;

static bms_feature_state_t s_feature;

static uint8_t charge_source_present(void)
{
    uint8_t present = 0u;
    if (bms_afe_get_charge_source_present(&present)) return present ? 1u : 0u;
    return bms_board_charge_source_present() ? 1u : 0u;
}

static void update_charge_session(void)
{
    /* SH36735xx has a dedicated C+/VCHGR charger detector. Unlike D008, the
     * session need not survive zero current by itself: when CHG is closed by a
     * low-temperature protection the charger detector remains the source truth.
     * Charge current is retained only as a secondary positive indication. */
    if (charge_source_present() || g_stCellInfoReport.u16Ichg != 0u)
        s_feature.charge_session_active = 1u;
    else
        s_feature.charge_session_active = 0u;

    if (g_stCellInfoReport.u16IDischg != 0u)
        s_feature.charge_session_active = 0u;
}

static void set_heater(uint8_t on)
{
    on = (on && bms_board_heater_supported() && bms_board_heater_allowed()) ? 1u : 0u;
    bms_board_heater_set(on);
    s_feature.heater_on = on;
    g_bms_system_status.bits.b1Status_Heat = on;
}

static void heater_idle(void)
{
    set_heater(0u);
    s_feature.heater_state = BMS_HEATER_IDLE;
}

static uint8_t heater_circuit_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t trip = g_tParam.protect.u16TmosOTp_Third;
    uint16_t recover = g_tParam.protect.u16TmosOTp_Rcv;

    if ((s == 0) || !s->heater_temp_valid)
    {
        s_feature.heater_overtemp_latched = 1u;
        heater_idle();
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    if (s_feature.heater_overtemp_latched)
    {
        if (recover != 0u && s->heater_temp_x10 <= recover)
            s_feature.heater_overtemp_latched = 0u;
    }
    else if (trip != 0u && s->heater_temp_x10 >= trip)
    {
        s_feature.heater_overtemp_latched = 1u;
    }

    if (s_feature.heater_overtemp_latched)
    {
        heater_idle();
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    bms_error_clear(BMS_ERROR_HEAT);
    return 1u;
}

static uint8_t heater_hard_fault(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    /* Charge/discharge UTP are intentionally excluded: low temperature is the
     * recoverable condition preheat exists to fix. */
    return (!bms_protection_params_valid() ||
            s_feature.openwire_fault_latched ||
            s_feature.openwire_suspected ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            f->b1CellOvp || f->b1BatOvp ||
            f->b1IchgOcp || f->b1IdischgOcp ||
            f->b1CellChgOtp || f->b1CellDischgOtp ||
            f->b1TmosOtp) ? 1u : 0u;
}

static uint8_t heater_demand(const bms_afe_feature_snapshot_t *s,
                             const bms_feature_params_t *config)
{
    uint16_t charge_utp_trip;

    if ((s == 0) || (config == 0) || !s->battery_temp_valid) return 0u;

    charge_utp_trip = g_tParam.protect.u16TchgUTp_Third;
    if ((charge_utp_trip != 0u) &&
        (s->battery_temp_min_x10 <= charge_utp_trip))
        return 1u;
    if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp) return 1u;

    if (s_feature.heater_state == BMS_HEATER_ACTIVE)
        return (s->battery_temp_min_x10 < config->heater_stop_x10) ? 1u : 0u;
    return (s->battery_temp_min_x10 < config->heater_start_x10) ? 1u : 0u;
}

static void service_heater(const bms_afe_feature_snapshot_t *s)
{
    bms_feature_params_t config;
    uint8_t demand;

    if ((s == 0) || !s->valid || !bms_board_heater_supported())
    {
        heater_idle();
        return;
    }

    if (!heater_circuit_safe(s)) return;

    if (!bms_config_get_features(&config) || !config.heater_enable ||
        !s->battery_temp_valid ||
        (g_tParam.protect.u16TchgUTp_Rcv != 0u &&
         config.heater_stop_x10 < g_tParam.protect.u16TchgUTp_Rcv) ||
        heater_hard_fault() || !s_feature.charge_session_active)
    {
        heater_idle();
        return;
    }

    demand = heater_demand(s, &config);

    if (s_feature.heater_state == BMS_HEATER_IDLE)
    {
        if (demand)
            s_feature.heater_state = BMS_HEATER_ARMING;
        set_heater(0u);
        return;
    }

    if (s_feature.heater_state == BMS_HEATER_ARMING)
    {
        if (!demand || !s_feature.charge_session_active)
        {
            heater_idle();
            return;
        }
        /* Hardware Charge-UTP may already have opened CHG before software sees
         * the charger. We still require a fresh zero-charge sample before heat. */
        if (g_stCellInfoReport.u16Ichg != 0u)
        {
            set_heater(0u);
            return;
        }
        set_heater(1u);
        if (s_feature.heater_on)
            s_feature.heater_state = BMS_HEATER_ACTIVE;
        else
            heater_idle();
        return;
    }

    if (!demand || !s_feature.charge_session_active ||
        g_stCellInfoReport.u16IDischg != 0u)
    {
        heater_idle();
        return;
    }

    set_heater(1u);
}

static uint8_t openwire_hard_fault(void)
{
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG)) ? 1u : 0u;
}

static uint8_t openwire_eligible(void)
{
    if (s_feature.heater_on) return 0u;
    if (g_stCellInfoReport.u16Ichg || g_stCellInfoReport.u16IDischg) return 0u;
    /* Do not interrupt an ordinary charge session for the periodic diagnostic.
     * A suspected open wire is different: it has priority over charge/heating
     * and must be diagnosed before voltage-dependent actions may resume. */
    if (!s_feature.openwire_suspected && s_feature.charge_session_active) return 0u;
    return openwire_hard_fault() ? 0u : 1u;
}

static void publish_balance(uint32_t mask)
{
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(mask & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((mask >> 16) & 0x00FFu);
    g_bms_system_status.bits.b1Status_Balance = mask ? 1u : 0u;
}

static uint8_t apply_balance_mask(uint32_t desired)
{
    uint32_t actual;

    s_feature.balance_requested_mask = desired;
    if (!bms_afe_set_balance_mask(desired))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        if (bms_afe_get_balance_mask(&actual)) publish_balance(actual);
        return 0u;
    }

    if (!bms_afe_get_balance_mask(&actual))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        return 0u;
    }

    bms_error_clear(BMS_ERROR_BALANCE);
    publish_balance(actual);
    return 1u;
}

static void service_openwire(void)
{
    bms_afe_diag_state_t state;
    bms_afe_openwire_result_t result;

    if (s_feature.openwire_active)
    {
        memset(&result, 0, sizeof(result));
        state = bms_afe_openwire_poll(&result);
        if (state == BMS_AFE_DIAG_READY)
        {
            s_feature.openwire_result = result;
            if (result.valid && result.determinate)
            {
                s_feature.openwire_fault_latched = result.open_cell_mask ? 1u : 0u;
                s_feature.openwire_suspected = s_feature.openwire_fault_latched;
                if (!s_feature.openwire_fault_latched)
                {
                    s_feature.balance_voltage_trusted = 0u;
                    s_feature.balance_trust_samples = 0u;
                    s_feature.balance_prev_cell_count = 0u;
                }
            }
            else
            {
                s_feature.openwire_suspected = 1u;
            }
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_PERIOD_SAMPLES;
        }
        else if (state == BMS_AFE_DIAG_ERROR)
        {
            s_feature.openwire_suspected = 1u;
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
        }
        return;
    }

    if (!s_feature.openwire_suspected && s_feature.openwire_cooldown_samples != 0u)
    {
        --s_feature.openwire_cooldown_samples;
        return;
    }

    if (!openwire_eligible())
    {
        s_feature.openwire_idle_samples = 0u;
        return;
    }

    if (!s_feature.openwire_suspected &&
        s_feature.openwire_idle_samples < (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES)
    {
        ++s_feature.openwire_idle_samples;
        return;
    }

    s_feature.balance_active = 0u;
    if (!apply_balance_mask(0u))
    {
        s_feature.openwire_idle_samples = 0u;
        return;
    }

    if (bms_afe_openwire_start())
    {
        s_feature.openwire_active = 1u;
        s_feature.openwire_idle_samples = 0u;
    }
    else
    {
        s_feature.openwire_suspected = 1u;
        s_feature.openwire_idle_samples = 0u;
        s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    }
}

static uint8_t balance_sample_plausible(const bms_afe_feature_snapshot_t *s)
{
    uint16_t vmin = 0xFFFFu;
    uint16_t vmax = 0u;
    uint8_t i;

    if ((s == 0) || !s->valid || (s->cell_count == 0u) ||
        (s->cell_count > BMS_AFE_FEATURE_MAX_CELLS))
        return 0u;

    for (i = 0u; i < s->cell_count; ++i)
    {
        uint16_t cell = g_stCellInfoReport.u16VCell[i];
        if (cell < BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV ||
            cell > BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV)
            return 0u;

        if (s_feature.balance_prev_cell_count == s->cell_count)
        {
            uint16_t previous = s_feature.balance_prev_cell_mv[i];
            uint16_t step = (cell >= previous) ?
                            (uint16_t)(cell - previous) :
                            (uint16_t)(previous - cell);
            if (step > BMS_BALANCE_CELL_MAX_STEP_MV) return 0u;
        }

        if (cell < vmin) vmin = cell;
        if (cell > vmax) vmax = cell;
    }

    if ((uint16_t)(vmax - vmin) > BMS_BALANCE_SUSPECT_DELTA_MV) return 0u;
    if (g_stCellInfoReport.u16VCellMin != vmin ||
        g_stCellInfoReport.u16VCellMax != vmax ||
        g_stCellInfoReport.u16VCellDelta != (uint16_t)(vmax - vmin))
        return 0u;

    for (i = 0u; i < s->cell_count; ++i)
        s_feature.balance_prev_cell_mv[i] = g_stCellInfoReport.u16VCell[i];
    s_feature.balance_prev_cell_count = s->cell_count;
    return 1u;
}

static void update_balance_voltage_trust(const bms_afe_feature_snapshot_t *s)
{
    uint16_t required = (uint16_t)BMS_BALANCE_TRUST_CONFIRM_SAMPLES;

    if (required == 0u) required = 1u;

    if (!balance_sample_plausible(s))
    {
        s_feature.balance_voltage_trusted = 0u;
        s_feature.balance_trust_samples = 0u;
        s_feature.balance_active = 0u;
        s_feature.openwire_suspected = 1u;
        return;
    }

    if (s_feature.openwire_suspected || s_feature.openwire_fault_latched)
    {
        s_feature.balance_voltage_trusted = 0u;
        s_feature.balance_trust_samples = 0u;
        return;
    }

    if (s_feature.balance_trust_samples < required)
        ++s_feature.balance_trust_samples;
    s_feature.balance_voltage_trusted =
        (s_feature.balance_trust_samples >= required) ? 1u : 0u;
}

static uint8_t balance_temperature_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t charge_ot_recover;
    uint16_t charge_ut_recover;
    uint16_t mos_ot_recover;

    if ((s == 0) || !s->battery_temp_valid || !s->mos_temp_valid) return 0u;

    charge_ot_recover = g_tParam.protect.u16TChgOTp_Rcv;
    charge_ut_recover = g_tParam.protect.u16TchgUTp_Rcv;
    mos_ot_recover = g_tParam.protect.u16TmosOTp_Rcv;

    if ((charge_ot_recover != 0u) &&
        (s->battery_temp_max_x10 >= charge_ot_recover))
        return 0u;
    if ((charge_ut_recover != 0u) &&
        (s->battery_temp_min_x10 <= charge_ut_recover))
        return 0u;
    if ((mos_ot_recover != 0u) &&
        (s->mos_temp_x10 >= mos_ot_recover))
        return 0u;
    return 1u;
}

static uint8_t balance_hard_fault(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    /* Cell OVP is intentionally excluded: after charge is blocked, verified
     * passive bleed remains a valid recovery path. */
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            f->b1CellUvp || f->b1BatUvp || f->b1BatOvp ||
            f->b1IchgOcp || f->b1IdischgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp ||
            f->b1TmosOtp) ? 1u : 0u;
}

static void service_balance(const bms_afe_feature_snapshot_t *s)
{
    bms_feature_params_t config;
    uint16_t threshold;
    uint32_t desired = 0u;
    uint8_t i;
    uint8_t allowed;

    allowed = (uint8_t)((s != 0) && s->valid &&
                        bms_board_balance_supported() &&
                        bms_config_get_features(&config) &&
                        config.balance_enable &&
                        s_feature.balance_voltage_trusted &&
                        !s_feature.openwire_active &&
                        !s_feature.openwire_fault_latched &&
                        !s_feature.openwire_suspected &&
                        (s_feature.heater_state == BMS_HEATER_IDLE) &&
                        s_feature.charge_session_active &&
                        balance_temperature_safe(s) &&
                        !balance_hard_fault());

    if (allowed)
    {
        threshold = s_feature.balance_active ?
                    config.balance_stop_delta_mv :
                    config.balance_start_delta_mv;

        if (g_stCellInfoReport.u16VCellMax >= config.balance_start_mv &&
            g_stCellInfoReport.u16VCellDelta >= threshold)
        {
            for (i = 0u; i < s->cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i)
            {
                uint16_t cell = g_stCellInfoReport.u16VCell[i];
                if (cell >= config.balance_start_mv &&
                    cell >= g_stCellInfoReport.u16VCellMin &&
                    (uint16_t)(cell - g_stCellInfoReport.u16VCellMin) >= threshold)
                    desired |= (1uL << i);
            }
        }
    }

    if (apply_balance_mask(desired))
        s_feature.balance_active = desired ? 1u : 0u;
    else
        s_feature.balance_active = 0u;
}

void bms_features_init(void)
{
    memset(&s_feature, 0, sizeof(s_feature));
    s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    bms_board_features_init();
    bms_board_heater_set(0u);
    g_bms_system_status.bits.b1Status_Heat = 0u;
    publish_balance(0u);
}

void bms_features_service(void)
{
    bms_afe_feature_snapshot_t s;

    memset(&s, 0, sizeof(s));
    if (!bms_afe_get_feature_snapshot(&s) || !s.valid)
    {
        bms_features_on_afe_invalid();
        return;
    }

    update_charge_session();
    update_balance_voltage_trust(&s);
    service_openwire();
    service_heater(&s);
    service_balance(&s);
}

void bms_features_on_afe_invalid(void)
{
    heater_idle();
    s_feature.charge_session_active = 0u;
    s_feature.balance_requested_mask = 0u;
    s_feature.balance_active = 0u;
    s_feature.balance_voltage_trusted = 0u;
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
    s_feature.openwire_active = 0u;
    s_feature.openwire_suspected = 1u;
    s_feature.openwire_idle_samples = 0u;

    if ((g_stCellInfoReport.u16BalanceFlag1 != 0u) ||
        (g_stCellInfoReport.u16BalanceFlag2 != 0u))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE))
            bms_error_raise(BMS_ERROR_BALANCE);
    }
}

uint8_t bms_features_heater_on(void) { return s_feature.heater_on; }
bms_heater_state_t bms_features_heater_state(void) { return s_feature.heater_state; }
uint8_t bms_features_charge_session_active(void) { return s_feature.charge_session_active; }
uint8_t bms_features_balance_voltage_trusted(void) { return s_feature.balance_voltage_trusted; }
uint8_t bms_features_openwire_suspected(void) { return s_feature.openwire_suspected; }

uint8_t bms_features_charge_hard_blocked(void)
{
    return (s_feature.openwire_active || s_feature.openwire_fault_latched) ? 1u : 0u;
}

uint8_t bms_features_charge_direction_blocked(void)
{
    return (s_feature.heater_state == BMS_HEATER_ARMING ||
            s_feature.heater_state == BMS_HEATER_ACTIVE) ? 1u : 0u;
}

uint8_t bms_features_charge_blocked(void)
{
    return (bms_features_charge_hard_blocked() ||
            bms_features_charge_direction_blocked()) ? 1u : 0u;
}

uint8_t bms_features_discharge_blocked(void)
{
    return (s_feature.openwire_active || s_feature.openwire_fault_latched) ? 1u : 0u;
}

uint8_t bms_features_openwire_active(void) { return s_feature.openwire_active; }
void bms_features_get_openwire_result(bms_afe_openwire_result_t *r)
{
    if (r) *r = s_feature.openwire_result;
}

uint32_t bms_features_diag_reasons(uint8_t charge)
{
    uint32_t reason = 0u;
    if (s_feature.openwire_active || s_feature.openwire_fault_latched)
        reason |= DIAG_BLOCK_OPENWIRE;
    if (charge && bms_features_charge_direction_blocked())
        reason |= DIAG_BLOCK_HEATER;
    return reason;
}
