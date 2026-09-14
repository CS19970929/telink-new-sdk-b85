#include "bms_afe.h"
#include <string.h>

/*
 * Common AFE safety guard, intentionally device-register agnostic.
 *
 * Rules aligned with the newer D013 framework:
 *   - any invalid snapshot inhibits both FET directions immediately;
 *   - recovery requires consecutive complete valid snapshots;
 *   - repeated communication failures trigger a bounded backend reinit;
 *   - requested FET state is retained but cannot bypass the inhibit;
 *   - the independent product output gate remains authoritative;
 *   - protection-configuration failure also inhibits output.
 */
#define BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT  3u
#define BMS_AFE_REINIT_TRIGGER                3u
#define BMS_AFE_REINIT_COOLDOWN_SAMPLES       25u /* 5 s at the current 200 ms sample period */

typedef struct
{
    uint8_t requested_charge_on;
    uint8_t requested_discharge_on;
    uint8_t output_enabled;
    uint8_t comm_inhibit;
    uint8_t valid_snapshot_streak;
    uint8_t comm_failures;
    uint8_t reinit_cooldown;
} bms_afe_guard_state_t;

static bms_afe_guard_state_t s_guard;

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
#define AFE_BACKEND_INIT()                         dvc1124_backend_init()
#define AFE_BACKEND_SAMPLE()                       dvc1124_backend_sample()
#define AFE_BACKEND_SLEEP()                        dvc1124_backend_sleep()
#define AFE_BACKEND_APPLY_PROTECTION()             dvc1124_backend_apply_protection_config()
#define AFE_BACKEND_SET_FETS(c, d)                 dvc1124_backend_set_fets((c), (d))
#define AFE_BACKEND_SET_OUTPUT_ENABLED(e)           dvc1124_backend_set_output_enabled((e))
#define AFE_BACKEND_GET_AUX(m)                     dvc1124_backend_get_aux_measurements((m))
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
#define AFE_BACKEND_INIT()                         sh3673510_bms_afe_init()
#define AFE_BACKEND_SAMPLE()                       sh3673510_bms_afe_sample()
#define AFE_BACKEND_SLEEP()                        sh3673510_bms_afe_sleep()
#define AFE_BACKEND_APPLY_PROTECTION()             sh3673510_bms_afe_apply_protection_config()
#define AFE_BACKEND_SET_FETS(c, d)                 sh3673510_bms_afe_set_fets((c), (d))
#define AFE_BACKEND_SET_OUTPUT_ENABLED(e)           sh3673510_bms_afe_set_output_enabled((e))
#define AFE_BACKEND_GET_AUX(m)                     sh3673510_bms_afe_get_aux_measurements((m))
#endif

static uint8_t bms_afe_guard_apply_requested(void)
{
    uint8_t charge_on = s_guard.requested_charge_on;
    uint8_t discharge_on = s_guard.requested_discharge_on;

    if (!s_guard.output_enabled || s_guard.comm_inhibit)
    {
        charge_on = 0u;
        discharge_on = 0u;
    }

    return AFE_BACKEND_SET_FETS(charge_on, discharge_on);
}

static void bms_afe_guard_inhibit(void)
{
    s_guard.comm_inhibit = 1u;
    s_guard.valid_snapshot_streak = 0u;
    /* Best effort. If the bus itself is unavailable this write can fail; the
     * inhibit still prevents any later software request from reopening FETs.
     * Hardware-enforced communication-loss shutdown remains a separate D008
     * validation item (DVC I2C WDT / board power-cycle policy). */
    (void)AFE_BACKEND_SET_FETS(0u, 0u);
}

static void bms_afe_guard_note_invalid_snapshot(void)
{
    bms_afe_guard_inhibit();

    if (s_guard.comm_failures != 0xFFu)
    {
        ++s_guard.comm_failures;
    }

    if ((s_guard.comm_failures >= BMS_AFE_REINIT_TRIGGER) &&
        (s_guard.reinit_cooldown == 0u))
    {
        /* Re-run the backend's bounded initialization/config/readback path.
         * Do not release the inhibit here: a fresh post-reinit measurement
         * must qualify on subsequent sample cycles. */
        s_guard.reinit_cooldown = BMS_AFE_REINIT_COOLDOWN_SAMPLES;
        s_guard.comm_failures = 0u;
        AFE_BACKEND_INIT();
        AFE_BACKEND_SET_OUTPUT_ENABLED(s_guard.output_enabled);
        (void)AFE_BACKEND_SET_FETS(0u, 0u);
    }
}

void bms_afe_init(void)
{
    memset(&s_guard, 0, sizeof(s_guard));
    s_guard.comm_inhibit = 1u;
    AFE_BACKEND_INIT();
    AFE_BACKEND_SET_OUTPUT_ENABLED(0u);
    (void)AFE_BACKEND_SET_FETS(0u, 0u);
}

void bms_afe_sample(void)
{
    bms_afe_aux_measurements_t measurements;

    if (s_guard.reinit_cooldown != 0u)
    {
        --s_guard.reinit_cooldown;
    }

    AFE_BACKEND_SAMPLE();
    memset(&measurements, 0, sizeof(measurements));

    if (!AFE_BACKEND_GET_AUX(&measurements))
    {
        bms_afe_guard_note_invalid_snapshot();
        return;
    }

    s_guard.comm_failures = 0u;
    if (s_guard.valid_snapshot_streak < BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
    {
        ++s_guard.valid_snapshot_streak;
    }

    if (s_guard.valid_snapshot_streak >= BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
    {
        s_guard.comm_inhibit = 0u;
    }

    if (!bms_afe_guard_apply_requested())
    {
        bms_afe_guard_note_invalid_snapshot();
    }
}

void bms_afe_sleep(void)
{
    /* Do not allow a pre-sleep valid snapshot to authorize outputs after wake. */
    bms_afe_guard_inhibit();
    s_guard.comm_failures = 0u;
    AFE_BACKEND_SLEEP();
}

uint8_t bms_afe_apply_protection_config(void)
{
    uint8_t ok = AFE_BACKEND_APPLY_PROTECTION();

    if (!ok)
    {
        bms_afe_guard_note_invalid_snapshot();
    }
    return ok;
}

uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    s_guard.requested_charge_on = charge_on ? 1u : 0u;
    s_guard.requested_discharge_on = discharge_on ? 1u : 0u;

    if (!bms_afe_guard_apply_requested())
    {
        bms_afe_guard_note_invalid_snapshot();
        return 0u;
    }
    return 1u;
}

void bms_afe_set_output_enabled(uint8_t enabled)
{
    s_guard.output_enabled = enabled ? 1u : 0u;
    AFE_BACKEND_SET_OUTPUT_ENABLED(s_guard.output_enabled);

    if (!s_guard.output_enabled)
    {
        (void)AFE_BACKEND_SET_FETS(0u, 0u);
        return;
    }

    if (!bms_afe_guard_apply_requested())
    {
        bms_afe_guard_note_invalid_snapshot();
    }
}

uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurements)
{
    if (measurements == 0) return 0u;
    if (!AFE_BACKEND_GET_AUX(measurements))
    {
        memset(measurements, 0, sizeof(*measurements));
        return 0u;
    }
    return 1u;
}
