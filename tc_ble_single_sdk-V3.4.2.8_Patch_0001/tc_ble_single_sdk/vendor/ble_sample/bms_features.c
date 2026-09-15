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
    uint8_t heater_overtemp_latched;
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
    return (bms_sw_protection_charge_blocked() ||
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

static void service_heater(const bms_afe_feature_snapshot_t *s)
{
    uint8_t charger;
    uint16_t trip;
    uint16_t recover;

    if (s == 0 || !s->valid || !bms_board_heater_supported()) {
        set_heater(0u);
        return;
    }

    charger = charge_source_present();
    trip = g_tParam.protect.u16TmosOTp_Third;
    recover = g_tParam.protect.u16TmosOTp_Rcv;

    if (!s->heater_temp_valid) {
        s_feature.heater_overtemp_latched = 1u;
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        set_heater(0u);
        return;
    }

    if (s_feature.heater_overtemp_latched) {
        if (recover == 0u || s->heater_temp_x10 <= recover)
            s_feature.heater_overtemp_latched = 0u;
    } else if (trip != 0u && s->heater_temp_x10 >= trip) {
        s_feature.heater_overtemp_latched = 1u;
    }

    if (s_feature.heater_overtemp_latched) {
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        set_heater(0u);
        return;
    }
    bms_error_clear(BMS_ERROR_HEAT);

    if (!charger || !s->battery_temp_valid ||
        bms_error_get(BMS_ERROR_AFE1) || bms_error_get(BMS_ERROR_TEMP_BREAK)) {
        set_heater(0u);
        return;
    }

    if (s_feature.heater_on)
        set_heater((s->battery_temp_min_x10 < BMS_HEATER_STOP_TEMP_X10) ? 1u : 0u);
    else
        set_heater((s->battery_temp_min_x10 < BMS_HEATER_START_TEMP_X10) ? 1u : 0u);
}

static uint8_t openwire_eligible(void)
{
    if (s_feature.heater_on || charge_source_present()) return 0u;
    if (g_stCellInfoReport.u16Ichg || g_stCellInfoReport.u16IDischg) return 0u;
    if (major_fault()) return 0u;
    return 1u;
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

    if (s_feature.openwire_cooldown_samples != 0u) {
        --s_feature.openwire_cooldown_samples;
        return;
    }
    if (!openwire_eligible()) {
        s_feature.openwire_idle_samples = 0u;
        return;
    }
    if (s_feature.openwire_idle_samples < (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES) {
        ++s_feature.openwire_idle_samples;
        return;
    }

    (void)bms_afe_set_balance_mask(0u);
    s_feature.balance_requested_mask = 0u;
    if (bms_afe_openwire_start()) {
        s_feature.openwire_active = 1u;
        s_feature.openwire_idle_samples = 0u;
    } else {
        s_feature.openwire_idle_samples = 0u;
        s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    }
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
    uint32_t desired = 0u;
    uint32_t effective = 0u;
    uint8_t i;

    if (s != 0 && s->valid &&
        !s_feature.openwire_active && !s_feature.openwire_fault_latched &&
        !s_feature.heater_on && !major_fault() &&
        g_stCellInfoReport.u16Ichg != 0u) {
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
        s_feature.balance_requested_mask = 0u;
        publish_balance(0u);
        return;
    }

    s_feature.balance_requested_mask = desired;
    if (!bms_afe_get_balance_mask(&effective)) effective = desired;
    bms_error_clear(BMS_ERROR_BALANCE);
    publish_balance(effective);
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
    bms_afe_feature_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (!bms_afe_get_feature_snapshot(&snapshot) || !snapshot.valid) {
        bms_features_on_afe_invalid();
        return;
    }
    service_heater(&snapshot);
    service_openwire();
    service_balance(&snapshot);
}

void bms_features_on_afe_invalid(void)
{
    set_heater(0u);
    s_feature.balance_requested_mask = 0u;
    s_feature.openwire_active = 0u;
    s_feature.openwire_idle_samples = 0u;
    publish_balance(0u);
}

uint8_t bms_features_heater_on(void) { return s_feature.heater_on; }
uint8_t bms_features_charge_blocked(void)
{
    return (s_feature.heater_on || s_feature.openwire_active ||
            s_feature.openwire_fault_latched) ? 1u : 0u;
}
uint8_t bms_features_discharge_blocked(void)
{
    return (s_feature.openwire_active || s_feature.openwire_fault_latched) ? 1u : 0u;
}
uint8_t bms_features_openwire_active(void) { return s_feature.openwire_active; }
void bms_features_get_openwire_result(bms_afe_openwire_result_t *result)
{
    if (result != 0) *result = s_feature.openwire_result;
}
