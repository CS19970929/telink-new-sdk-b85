#include "bms_supervisor.h"
#include "bms_measurement.h"
#include "bms_diagnostics.h"
#include "bms_sw_protection.h"
#include "bms_actuator.h"
#include "bms_fuse.h"
#include "bms_safety_config.h"
#include <string.h>

static bms_start_state_t g_start_state;
static uint32_t g_heartbeat_ms[8];
static uint32_t g_seen_tasks;
static uint8_t g_valid_frames;
static uint32_t g_last_valid_frame_timestamp_ms;
static uint8_t g_have_valid_frame_timestamp;
static uint8_t g_safety_init_ok;
static uint8_t g_suspicious_reset;
static uint16_t g_reset_reason;

static uint8_t task_index(uint32_t task)
{
    uint8_t i = 0u;
    while (((task >> i) & 1u) == 0u && i < 7u) ++i;
    return i;
}

void bms_supervisor_init(uint32_t now_ms, uint16_t reset_reason, uint8_t reset_suspicious)
{
    uint8_t i;
    g_start_state = BMS_START_IO_SAFE;
    g_seen_tasks = 0u;
    g_valid_frames = 0u;
    g_last_valid_frame_timestamp_ms = 0u;
    g_have_valid_frame_timestamp = 0u;
    g_safety_init_ok = 0u;
    g_suspicious_reset = reset_suspicious ? 1u : 0u;
    g_reset_reason = reset_reason;
    for (i = 0u; i < 8u; ++i) g_heartbeat_ms[i] = now_ms;
    bms_actuator_set_safety_ready(0u);
    bms_set_global_inhibit(BMS_INHIBIT_INIT, 1u);
    bms_set_charge_inhibit(BMS_INHIBIT_INIT, 1u);
    bms_set_discharge_inhibit(BMS_INHIBIT_INIT, 1u);
    if (g_suspicious_reset) {
        bms_set_global_inhibit(BMS_INHIBIT_WDT_RESET, 1u);
    }
}

void bms_supervisor_begin_afe_startup(void)
{
    if (g_start_state == BMS_START_IO_SAFE)
        g_start_state = BMS_START_AFE_WAIT_READY;
}

void bms_supervisor_note_afe_ready(uint8_t ok)
{
    if (g_start_state == BMS_START_LOCKED) return;
    g_start_state = ok ? BMS_START_AFE_CONFIG : BMS_START_LOCKED;
}

void bms_supervisor_note_afe_config(uint8_t ok)
{
    if (g_start_state == BMS_START_LOCKED) return;
    g_start_state = ok ? BMS_START_AFE_VERIFY : BMS_START_LOCKED;
}

void bms_supervisor_note_afe_verify(uint8_t ok)
{
    if (g_start_state == BMS_START_LOCKED) return;
    g_start_state = ok ? BMS_START_SAMPLE_VALIDATE : BMS_START_LOCKED;
}

void bms_supervisor_heartbeat(uint32_t task, uint32_t now_ms)
{
    uint8_t index = task_index(task);
    g_seen_tasks |= task;
    g_heartbeat_ms[index] = now_ms;
}

void bms_supervisor_update(uint32_t now_ms)
{
    uint8_t valid = bms_measurement_is_fresh_and_complete(now_ms);

    if (g_start_state == BMS_START_SAMPLE_VALIDATE) {
        if (valid && bms_diagnostics_blocking_faults() == 0u) {
            const bms_measurement_t *m = bms_measurement_get();
            if (!g_have_valid_frame_timestamp ||
                m->timestamp_ms != g_last_valid_frame_timestamp_ms) {
                g_last_valid_frame_timestamp_ms = m->timestamp_ms;
                g_have_valid_frame_timestamp = 1u;
                if (g_valid_frames < 0xFFu) ++g_valid_frames;
            }
            if (g_valid_frames >= BMS_REQUIRED_VALID_FRAMES)
                g_start_state = BMS_START_PROTECTION_CHECK;
        } else {
            g_valid_frames = 0u;
            g_have_valid_frame_timestamp = 0u;
        }
    }
    if (g_start_state == BMS_START_PROTECTION_CHECK) {
        if (valid && bms_sw_protection_active_mask() == 0u &&
            bms_fuse_get_state() != FUSE_FIRED &&
            bms_fuse_get_state() != FUSE_FAILED) {
            g_start_state = BMS_START_READY;
            g_safety_init_ok = 1u;
            bms_set_global_inhibit(BMS_INHIBIT_INIT, 0u);
            bms_set_charge_inhibit(BMS_INHIBIT_INIT, 0u);
            bms_set_discharge_inhibit(BMS_INHIBIT_INIT, 0u);
            bms_set_global_inhibit(BMS_INHIBIT_WDT_RESET, 0u);
#if BMS_POWER_PATH_HW_VERIFIED_ENABLE
            bms_set_global_inhibit(BMS_INHIBIT_HW_UNVERIFIED, 0u);
            bms_set_charge_inhibit(BMS_INHIBIT_HW_UNVERIFIED, 0u);
            bms_set_discharge_inhibit(BMS_INHIBIT_HW_UNVERIFIED, 0u);
#endif
            bms_actuator_set_safety_ready(1u);
        } else g_start_state = BMS_START_LOCKED;
    }
    if (g_start_state == BMS_START_LOCKED) {
        g_safety_init_ok = 0u;
        bms_actuator_set_safety_ready(0u);
        bms_set_global_inhibit(BMS_INHIBIT_INIT, 1u);
    }
}

void bms_supervisor_require_revalidation(void)
{
    g_valid_frames = 0u;
    g_have_valid_frame_timestamp = 0u;
    g_safety_init_ok = 0u;
    g_start_state = BMS_START_SAMPLE_VALIDATE;
    bms_actuator_set_safety_ready(0u);
    bms_set_global_inhibit(BMS_INHIBIT_INIT, 1u);
}

uint8_t bms_supervisor_watchdog_allowed(uint32_t now_ms)
{
    uint8_t i;
    static const uint32_t timeout_ms[8] = {1800u, 1800u, 1500u, 1500u, 500u, 1500u, 1500u, 5000u};
    if ((g_seen_tasks & BMS_TASK_REQUIRED_MASK) != BMS_TASK_REQUIRED_MASK) {
        /* Startup I2C/config may be synchronous; do not force a reset before init returns. */
        return (g_start_state < BMS_START_SAMPLE_VALIDATE) ? 1u : 0u;
    }
    for (i = 0u; i < 8u; ++i)
        if ((uint32_t)(now_ms - g_heartbeat_ms[i]) > timeout_ms[i]) return 0u;
    return 1u;
}

uint8_t bms_supervisor_safety_init_ok(void) { return g_safety_init_ok; }
bms_start_state_t bms_supervisor_state(void) { return g_start_state; }
uint16_t bms_supervisor_reset_reason(void) { return g_reset_reason; }
