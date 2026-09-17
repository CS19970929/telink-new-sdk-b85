#include "bms_diag.h"
#include "bms_features.h"

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

typedef struct {
    uint8_t heater_on;
    uint8_t heater_fuse_fired;
    uint16_t heater_off_hot_samples;
    uint8_t openwire_active;
    uint8_t openwire_fault_latched;
    uint16_t openwire_idle_samples;
    uint16_t openwire_cooldown_samples;
    uint32_t balance_requested_mask;
    bms_afe_openwire_result_t openwire_result;
} bms_feature_state_t;

static bms_feature_state_t s_feature;

static uint8_t major_fault(void)
{
    return (!bms_protection_params_valid() ||
            bms_sw_protection_charge_blocked() ||
            bms_sw_protection_discharge_blocked() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT)) ? 1u : 0u;
}

static uint8_t charge_source_present(void)
{
    uint8_t present = 0u;
    if (bms_afe_get_charge_source_present(&present)) return present ? 1u : 0u;
    return bms_board_charge_source_present() ? 1u : 0u;
}

static void set_heater(uint8_t on)
{
    on = (on && bms_board_heater_supported()) ? 1u : 0u;
    bms_board_heater_set(on);
    s_feature.heater_on = on;
    g_bms_system_status.bits.b1Status_Heat = on;
}

static uint16_t heater_confirm_samples(void)
{
    uint32_t confirm_ms = bms_board_heater_off_fault_confirm_ms();
    uint32_t samples;

    if (confirm_ms == 0u) return 1u;
    samples = (confirm_ms + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) /
              BMS_FEATURE_SERVICE_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

static void fire_heater_fuse(void)
{
    set_heater(0u);
    if (!s_feature.heater_fuse_fired)
    {
        bms_board_heater_fuse_fire();
        s_feature.heater_fuse_fired = 1u;
    }
    if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
}

/*
 * Heater-circuit safety is independent of normal power-MOS OTP.
 *
 * D008 GP1 measures the heater MOS area. If software commands PA1/MCC-EN-HT
 * OFF but GP1 remains abnormally hot for the board-defined confirmation time,
 * the heater power path is treated as stuck/failed and PD4/MCC-EN-RF fires the
 * irreversible heater fuse. A hot GP1 while heating first forces the command
 * OFF; only continued heat while OFF can progress to the irreversible action.
 */
static uint8_t heater_circuit_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t trip;
    uint16_t required;

    if (s_feature.heater_fuse_fired)
    {
        set_heater(0u);
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    if ((s == 0) || !s->heater_temp_valid)
    {
        s_feature.heater_off_hot_samples = 0u;
        set_heater(0u);
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    trip = bms_board_heater_off_fault_temp_x10();
    if (!bms_board_heater_fuse_supported() || (trip == 0u))
    {
        s_feature.heater_off_hot_samples = 0u;
        bms_error_clear(BMS_ERROR_HEAT);
        return 1u;
    }

    if (s->heater_temp_x10 < trip)
    {
        s_feature.heater_off_hot_samples = 0u;
        bms_error_clear(BMS_ERROR_HEAT);
        return 1u;
    }

    /* GP1 is already too hot. A commanded heater must be shut down first. */
    if (s_feature.heater_on)
    {
        s_feature.heater_off_hot_samples = 0u;
        set_heater(0u);
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    /* Heater command is OFF but the heater-MOS area stays hot: confirm before
     * the irreversible fuse action so one noisy sample cannot fire PD4. */
    required = heater_confirm_samples();
    if (s_feature.heater_off_hot_samples < required)
        ++s_feature.heater_off_hot_samples;
    if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
    set_heater(0u);

    if (s_feature.heater_off_hot_samples >= required)
        fire_heater_fuse();
    return 0u;
}

static void service_heater(const bms_afe_feature_snapshot_t *s)
{
    uint8_t charger;

    if (s == 0 || !s->valid || !bms_board_heater_supported())
    {
        set_heater(0u);
        return;
    }

    if (!heater_circuit_safe(s)) return;

    charger = charge_source_present();
    if (!charger || !s->battery_temp_valid || bms_error_get(BMS_ERROR_AFE1) ||
        bms_error_get(BMS_ERROR_TEMP_BREAK))
    {
        set_heater(0u);
        return;
    }

    /* Battery heating always uses the colder of GP2/GP3. */
    if (s_feature.heater_on)
        set_heater((s->battery_temp_min_x10 < BMS_HEATER_STOP_TEMP_X10) ? 1u : 0u);
    else
        set_heater((s->battery_temp_min_x10 < BMS_HEATER_START_TEMP_X10) ? 1u : 0u);
}

static uint8_t openwire_eligible(void)
{
    if (s_feature.heater_on || charge_source_present()) return 0u;
    if (g_stCellInfoReport.u16Ichg || g_stCellInfoReport.u16IDischg) return 0u;
    return major_fault() ? 0u : 1u;
}

static void service_openwire(void)
{
    bms_afe_diag_state_t state;
    bms_afe_openwire_result_t result;
    if (s_feature.openwire_active) {
        memset(&result, 0, sizeof(result));
        state = bms_afe_openwire_poll(&result);
        if (state == BMS_AFE_DIAG_READY) {
            s_feature.openwire_result = result;
            if (result.valid && result.determinate)
                s_feature.openwire_fault_latched = result.open_cell_mask ? 1u : 0u;
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_PERIOD_SAMPLES;
        } else if (state == BMS_AFE_DIAG_ERROR) {
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_PERIOD_SAMPLES;
        }
        return;
    }
    if (s_feature.openwire_cooldown_samples != 0u) { --s_feature.openwire_cooldown_samples; return; }
    if (!openwire_eligible()) { s_feature.openwire_idle_samples = 0u; return; }
    if (s_feature.openwire_idle_samples < (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES) { ++s_feature.openwire_idle_samples; return; }
    (void)bms_afe_set_balance_mask(0u);
    s_feature.balance_requested_mask = 0u;
    if (bms_afe_openwire_start()) { s_feature.openwire_active = 1u; s_feature.openwire_idle_samples = 0u; }
    else { s_feature.openwire_idle_samples = 0u; s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES; }
}

static void publish_balance(uint32_t mask)
{
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(mask & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((mask >> 16) & 0x00FFu);
    g_bms_system_status.bits.b1Status_Balance = mask ? 1u : 0u;
}

static void service_balance(const bms_afe_feature_snapshot_t *s)
{
    uint16_t threshold;
    uint32_t desired = 0u, effective = 0u;
    uint8_t i;
    if (s != 0 && s->valid && !s_feature.openwire_active && !s_feature.openwire_fault_latched &&
        !s_feature.heater_on && !major_fault() && g_stCellInfoReport.u16Ichg != 0u) {
        threshold = g_tParam.protect.u16VdeltaOvp_First;
        if (threshold != 0u && g_stCellInfoReport.u16VCellDelta >= threshold) {
            for (i = 0u; i < s->cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i) {
                uint16_t cell = g_stCellInfoReport.u16VCell[i];
                if (cell >= g_stCellInfoReport.u16VCellMin &&
                    (uint16_t)(cell - g_stCellInfoReport.u16VCellMin) >= threshold)
                    desired |= (1uL << i);
            }
        }
    }
    if (!bms_afe_set_balance_mask(desired)) {
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        publish_balance(0u); return;
    }
    s_feature.balance_requested_mask = desired;
    if (!bms_afe_get_balance_mask(&effective)) effective = desired;
    bms_error_clear(BMS_ERROR_BALANCE);
    publish_balance(effective);
}

void bms_features_init(void)
{
    memset(&s_feature, 0, sizeof(s_feature));
    bms_sw_protection_init();
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
    service_heater(&s);
    service_openwire();
    service_balance(&s);
}

void bms_features_on_afe_invalid(void)
{
    set_heater(0u);
    s_feature.heater_off_hot_samples = 0u;
    s_feature.balance_requested_mask = 0u;
    s_feature.openwire_active = 0u;
    s_feature.openwire_idle_samples = 0u;
    publish_balance(0u);
    if (s_feature.heater_fuse_fired && !bms_error_get(BMS_ERROR_HEAT))
        bms_error_raise(BMS_ERROR_HEAT);
}

uint8_t bms_features_heater_on(void) { return s_feature.heater_on; }
uint8_t bms_features_heater_fuse_fired(void) { return s_feature.heater_fuse_fired; }
uint8_t bms_features_charge_blocked(void) { return (!bms_protection_params_valid() || s_feature.heater_on || s_feature.openwire_active || s_feature.openwire_fault_latched) ? 1u : 0u; }
uint8_t bms_features_discharge_blocked(void) { return (!bms_protection_params_valid() || s_feature.openwire_active || s_feature.openwire_fault_latched) ? 1u : 0u; }
uint8_t bms_features_openwire_active(void) { return s_feature.openwire_active; }
void bms_features_get_openwire_result(bms_afe_openwire_result_t *r) { if (r) *r = s_feature.openwire_result; }

uint32_t bms_features_diag_reasons(uint8_t charge)
{
    uint32_t reason = 0u;
    if (s_feature.openwire_active || s_feature.openwire_fault_latched) reason |= DIAG_BLOCK_OPENWIRE;
    if (charge && s_feature.heater_on) reason |= DIAG_BLOCK_HEATER;
    return reason;
}
