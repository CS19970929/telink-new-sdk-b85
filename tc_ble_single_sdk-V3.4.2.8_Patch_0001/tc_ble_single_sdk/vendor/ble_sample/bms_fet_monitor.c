#include "bms_fet_monitor.h"
#include "bms_actuator.h"
#include "bms_safety_config.h"
#include <string.h>

static bms_fet_monitor_t g_monitor[BMS_FET_MONITOR_COUNT];

void bms_fet_monitor_init(void)
{
    memset(g_monitor, 0, sizeof(g_monitor));
}

static uint8_t bms_path_commanded_off(bms_fet_path_t path)
{
    const bms_actuator_state_t *a = bms_actuator_get_state();
    if (path == BMS_FET_CHARGE) return a->charge_on ? 0u : 1u;
    if (path == BMS_FET_DISCHARGE) return a->discharge_on ? 0u : 1u;
    return a->ctlc_on ? 0u : 1u;
}

static uint8_t bms_danger_current(bms_fet_path_t path, int32_t current_ma)
{
    if (path == BMS_FET_CHARGE) return current_ma > BMS_FET_DANGER_CURRENT_MA;
    if (path == BMS_FET_DISCHARGE) return current_ma < -BMS_FET_DANGER_CURRENT_MA;
    return (current_ma > BMS_FET_DANGER_CURRENT_MA ||
            current_ma < -BMS_FET_DANGER_CURRENT_MA);
}

void bms_fet_monitor_update(const bms_measurement_t *m, uint32_t now_ms,
                            uint8_t afe_charge_on, uint8_t afe_discharge_on)
{
    uint8_t i;
    if (m == 0) return;
    for (i = 0u; i < BMS_FET_MONITOR_COUNT; ++i) {
        bms_fet_monitor_t *s = &g_monitor[i];
        uint8_t requested_off = bms_path_commanded_off((bms_fet_path_t)i);
        uint8_t logical_off = (i == BMS_FET_CHARGE) ? !afe_charge_on :
                              ((i == BMS_FET_DISCHARGE) ? !afe_discharge_on : 1u);
        uint8_t feedback_off = bms_fet_hw_feedback_supported((bms_fet_path_t)i) ?
                               bms_fet_hw_is_off((bms_fet_path_t)i) : logical_off;
        uint8_t danger = bms_danger_current((bms_fet_path_t)i, m->current_ma);

        switch (s->state) {
        case FET_MONITOR_IDLE:
        case FET_OFF_CONFIRMED:
            if (!requested_off) {
                s->was_commanded_on = 1u;
                s->state = FET_MONITOR_IDLE;
            } else if (s->was_commanded_on || danger) {
                s->state = FET_OFF_REQUESTED;
                s->requested_ms = now_ms;
                s->danger_frames = 0u;
                s->was_commanded_on = 0u;
            }
            break;
        case FET_OFF_REQUESTED:
            s->state = FET_OFF_GRACE;
            break;
        case FET_OFF_GRACE:
            if (!requested_off) s->state = FET_MONITOR_IDLE;
            else if ((uint32_t)(now_ms - s->requested_ms) >= BMS_FET_OFF_GRACE_MS)
                s->state = FET_OFF_VERIFY;
            break;
        case FET_OFF_VERIFY:
            if (!requested_off) s->state = FET_MONITOR_IDLE;
            else if (!danger && feedback_off) {
                s->state = FET_OFF_CONFIRMED;
                s->danger_frames = 0u;
            } else {
                if (s->danger_frames < 0xFFu) ++s->danger_frames;
                if (s->danger_frames >= BMS_FET_OFF_CONFIRM_FRAMES) {
                    s->state = FET_OFF_FAILED;
                    bms_set_global_inhibit(BMS_INHIBIT_FET_OFF_FAILED, 1u);
                }
            }
            break;
        case FET_OFF_FAILED:
        default:
            bms_set_global_inhibit(BMS_INHIBIT_FET_OFF_FAILED, 1u);
            break;
        }
    }
}

uint8_t bms_fet_monitor_any_failed(void)
{
    uint8_t i;
    for (i = 0u; i < BMS_FET_MONITOR_COUNT; ++i)
        if (g_monitor[i].state == FET_OFF_FAILED) return 1u;
    return 0u;
}

const bms_fet_monitor_t *bms_fet_monitor_get(bms_fet_path_t path)
{
    return ((uint8_t)path < BMS_FET_MONITOR_COUNT) ? &g_monitor[path] : 0;
}
