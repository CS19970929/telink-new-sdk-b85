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

typedef struct
{
    uint8_t heater_on;
    uint8_t heater_overtemp_latched;
    uint8_t openwire_active;
    uint16_t openwire_idle_samples;
    uint16_t openwire_cooldown_samples;
    uint32_t balance_requested_mask;
    bms_afe_openwire_result_t openwire_result;
} bms_feature_state_t;

static bms_feature_state_t s_feature;

static uint8_t bms_features_major_fault(void)
{
    return (bms_sw_protection_charge_blocked() ||
            bms_sw_protection_discharge_blocked() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT)) ? 1u : 0u;
}

static void bms_features_set_heater(uint8_t on)
{
    on = (on && bms_board_heater_supported()) ? 1u : 0u;
    if (on != s_feature.heater_on)
    {
        bms_board_heater_set(on);
        s_feature.heater_on = on;
    }
    g_bms_system_status.bits.b1Status_Heat = s_feature.heater_on;
}

static void bms_features_service_heater(const bms_afe_feature_snapshot_t *snapshot)
{
    uint8_t charger;
    uint16_t trip;
    uint16_t recover;

    if ((snapshot == 0) || !snapshot->valid || !bms_board_heater_supported())
    {
        bms_features_set_heater(0u);
        return;
    }

    charger = bms_board_charge_source_present();
    trip = g_tParam.protect.u16TmosOTp_Third;
    recover = g_tParam.protect.u16TmosOTp_Rcv;

    /* Heater-MOS sensor failure is fail-safe: heater is forced off. */
    if (!snapshot->heater_temp_valid)
    {
        s_feature.heater_overtemp_latched = 1u;
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        bms_features_set_heater(0u);
        return;
    }

    if (s_feature.heater_overtemp_latched)
    {
        if ((recover == 0u) || (snapshot->heater_temp_x10 <= recover))
            s_feature.heater_overtemp_latched = 0u;
    }
    else if ((trip != 0u) && (snapshot->heater_temp_x10 >= trip))
    {
        s_feature.heater_overtemp_latched = 1u;
    }

    if (s_feature.heater_overtemp_latched)
    {
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        bms_features_set_heater(0u);
        return;
    }
    bms_error_clear(BMS_ERROR_HEAT);

    /*
     * Unified charge-heating policy:
     *   - a real product charge-source input must be active;
     *   - start only below 0 degC;
     *   - CHG MOS is blocked for the whole heating state by bms_afe_guard;
     *   - stop at +5 degC to avoid 0 degC chatter;
     *   - charger removal / bad temperature / AFE fault turns heater off.
     */
    if (!charger || !snapshot->battery_temp_valid ||
        bms_error_get(BMS_ERROR_AFE1) || bms_error_get(BMS_ERROR_TEMP_BREAK))
    {
        bms_features_set_heater(0u);
        return;
    }

    if (s_feature.heater_on)
    {
        if (snapshot->battery_temp_min_x10 >= BMS_HEATER_STOP_TEMP_X10)
            bms_features_set_heater(0u);
    }
    else if (snapshot->battery_temp_min_x10 < BMS_HEATER_START_TEMP_X10)
    {
        bms_features_set_heater(1u);
    }
}

static uint8_t bms_features_openwire_eligible(void)
{
    if (s_feature.heater_on || bms_board_charge_source_present()) return 0u;
    if (g_stCellInfoReport.u16Ichg || g_stCellInfoReport.u16IDischg) return 0u;
    if (bms_features_major_fault()) return 0u;
    return 1u;
}

static void bms_features_service_openwire(void)
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
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_PERIOD_SAMPLES;
        }
        else if (state == BMS_AFE_DIAG_ERROR)
        {
            memset(&s_feature.openwire_result, 0, sizeof(s_feature.openwire_result));
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_PERIOD_SAMPLES;
        }
        return;
    }

    if (s_feature.openwire_cooldown_samples != 0u)
    {
        --s_feature.openwire_cooldown_samples;
        return;
    }

    if (!bms_features_openwire_eligible())
    {
        s_feature.openwire_idle_samples = 0u;
        return;
    }

    if (s_feature.openwire_idle_samples < (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES)
    {
        ++s_feature.openwire_idle_samples;
        return;
    }

    /* Never run open-wire stimulus while balancing. */
    (void)bms_afe_set_balance_mask(0u);
    s_feature.balance_requested_mask = 0u;
    if (bms_afe_openwire_start())
    {
        s_feature.openwire_active = 1u;
        s_feature.openwire_idle_samples = 0u;
    }
    else
    {
        s_feature.openwire_idle_samples = 0u;
        s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    }
}

static void bms_features_publish_balance(uint32_t mask)
{
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(mask & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((mask >> 16) & 0x00FFu);
    g_bms_system_status.bits.b1Status_Balance = mask ? 1u : 0u;
}

static void bms_features_service_balance(const bms_afe_feature_snapshot_t *snapshot)
{
    uint16_t threshold;
    uint32_t desired = 0u;
    uint32_t effective = 0u;
    uint8_t i;

    if ((snapshot == 0) || !snapshot->valid || s_feature.openwire_active ||
        s_feature.heater_on || bms_features_major_fault() ||
        g_stCellInfoReport.u16Ichg == 0u)
    {
        desired = 0u;
    }
    else
    {
        /* V1 compatibility: reuse the existing first-level delta threshold as
         * the balance start delta. A dedicated persisted balance profile can
         * replace this later without changing the state machine/backend API. */
        threshold = g_tParam.protect.u16VdeltaOvp_First;
        if (threshold != 0u && g_stCellInfoReport.u16VCellDelta >= threshold)
        {
            for (i = 0u; i < snapshot->cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i)
            {
                uint16_t cell = g_stCellInfoReport.u16VCell[i];
                if (cell >= g_stCellInfoReport.u16VCellMin &&
                    (uint16_t)(cell - g_stCellInfoReport.u16VCellMin) >= threshold)
                    desired |= (1uL << i);
            }
        }
    }

    if (!bms_afe_set_balance_mask(desired))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        s_feature.balance_requested_mask = 0u;
        bms_features_publish_balance(0u);
        return;
    }

    s_feature.balance_requested_mask = desired;
    if (!bms_afe_get_balance_mask(&effective)) effective = desired;
    bms_error_clear(BMS_ERROR_BALANCE);
    bms_features_publish_balance(effective);
}

void bms_features_init(void)
{
    memset(&s_feature, 0, sizeof(s_feature));
    s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    bms_board_features_init();
    bms_board_heater_set(0u);
    g_bms_system_status.bits.b1Status_Heat = 0u;
    bms_features_publish_balance(0u);
}

void bms_features_service(void)
{
    bms_afe_feature_snapshot_t snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    if (!bms_afe_get_feature_snapshot(&snapshot) || !snapshot.valid)
    {
        bms_features_on_afe_invalid();
        return;
    }

    bms_features_service_heater(&snapshot);
    bms_features_service_openwire();
    bms_features_service_balance(&snapshot);
}

void bms_features_on_afe_invalid(void)
{
    bms_features_set_heater(0u);
    s_feature.balance_requested_mask = 0u;
    s_feature.openwire_active = 0u;
    s_feature.openwire_idle_samples = 0u;
    bms_features_publish_balance(0u);
}

uint8_t bms_features_heater_on(void)
{
    return s_feature.heater_on;
}

uint8_t bms_features_charge_blocked(void)
{
    return (s_feature.heater_on || s_feature.openwire_active) ? 1u : 0u;
}

uint8_t bms_features_discharge_blocked(void)
{
    return s_feature.openwire_active ? 1u : 0u;
}

uint8_t bms_features_openwire_active(void)
{
    return s_feature.openwire_active;
}

void bms_features_get_openwire_result(bms_afe_openwire_result_t *result)
{
    if (result != 0) *result = s_feature.openwire_result;
}
