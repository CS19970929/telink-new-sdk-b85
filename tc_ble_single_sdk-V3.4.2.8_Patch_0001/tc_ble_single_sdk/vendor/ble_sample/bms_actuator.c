#include "bms_actuator.h"
#include "bms_safety_config.h"
#include <string.h>

static uint32_t g_charge_inhibit;
static uint32_t g_discharge_inhibit;
static uint32_t g_global_inhibit;
static uint32_t g_global_clear_since;
static uint8_t g_charge_request;
static uint8_t g_discharge_request;
static uint8_t g_precharge_request;
static uint8_t g_safety_ready;
static bms_actuator_state_t g_state;

void bms_actuator_init(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_charge_inhibit = BMS_INHIBIT_INIT | BMS_INHIBIT_HW_UNVERIFIED;
    g_discharge_inhibit = BMS_INHIBIT_INIT | BMS_INHIBIT_HW_UNVERIFIED;
    g_global_inhibit = BMS_INHIBIT_INIT | BMS_INHIBIT_HW_UNVERIFIED;
    g_charge_request = 0u;
    g_discharge_request = 0u;
    g_precharge_request = 0u;
    g_safety_ready = 0u;
    g_global_clear_since = 0u;
    (void)bms_actuator_hw_apply(&g_state);
}

static void bms_mask_set(uint32_t *mask, uint32_t reason, uint8_t active)
{
    if (active) {
        *mask |= reason;
    } else {
        *mask &= ~reason;
    }
}

void bms_set_charge_inhibit(uint32_t reason, uint8_t active) { bms_mask_set(&g_charge_inhibit, reason, active); }
void bms_set_discharge_inhibit(uint32_t reason, uint8_t active) { bms_mask_set(&g_discharge_inhibit, reason, active); }
void bms_set_global_inhibit(uint32_t reason, uint8_t active) { bms_mask_set(&g_global_inhibit, reason, active); }
uint32_t bms_get_charge_inhibit(void) { return g_charge_inhibit; }
uint32_t bms_get_discharge_inhibit(void) { return g_discharge_inhibit; }
uint32_t bms_get_global_inhibit(void) { return g_global_inhibit; }
void bms_actuator_request_charge(uint8_t enable) { g_charge_request = enable ? 1u : 0u; }
void bms_actuator_request_discharge(uint8_t enable) { g_discharge_request = enable ? 1u : 0u; }
void bms_actuator_request_precharge(uint8_t enable) { g_precharge_request = enable ? 1u : 0u; }
void bms_actuator_set_safety_ready(uint8_t ready) { g_safety_ready = ready ? 1u : 0u; }

void bms_actuator_update(uint32_t now_ms)
{
    bms_actuator_state_t next;
    uint8_t global_clear;

    memset(&next, 0, sizeof(next));
    global_clear = (g_safety_ready && (g_global_inhibit == 0u));
    if (!global_clear) {
        g_global_clear_since = now_ms;
    }

    if (global_clear &&
        ((uint32_t)(now_ms - g_global_clear_since) >= BMS_RECOVERY_GUARD_MS)) {
        next.ctlc_on = 1u;
        next.charge_on = (g_charge_inhibit == 0u) ? g_charge_request : 0u;
        next.discharge_on = (g_discharge_inhibit == 0u) ? g_discharge_request : 0u;
#if BMS_PRECHARGE_HW_ENABLE
        next.precharge_on = (g_charge_inhibit == 0u &&
                             g_discharge_inhibit == 0u) ? g_precharge_request : 0u;
#else
        next.precharge_on = 0u;
#endif
    }

#if !BMS_POWER_PATH_HW_VERIFIED_ENABLE
    memset(&next, 0, sizeof(next));
#endif
    g_state = next;
    if (!bms_actuator_hw_apply(&g_state)) {
        g_charge_inhibit |= BMS_INHIBIT_AFE_COMM;
        g_discharge_inhibit |= BMS_INHIBIT_AFE_COMM;
        g_global_inhibit |= BMS_INHIBIT_AFE_COMM;
        memset(&g_state, 0, sizeof(g_state));
        (void)bms_actuator_hw_apply(&g_state);
    }
}

const bms_actuator_state_t *bms_actuator_get_state(void)
{
    return &g_state;
}
