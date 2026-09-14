#include "dvc1124.h"
#include "dvc1124_config_store.h"
#include "bms_afe.h"
#include "dvc1124_safe_bms.h"
#include "bms_error.h"
#include <string.h>

/*
 * D013-style supervisory layer for the HS-D008/DVC1124 product.
 *
 * This file deliberately does not know D013 GPIOs or SH3673510 registers.
 * It wraps the existing DVC1124 bms_afe_* implementation and owns only the
 * cross-cutting safety state needed above the device driver:
 *   - requested FET state vs. effective FET state;
 *   - communication-failure output inhibit;
 *   - release only after consecutive complete valid snapshots;
 *   - bounded reinitialization cadence;
 *   - SCD software latch with fail-safe default recovery policy.
 */

#ifndef DVC1124_SAFE_REINIT_TRIGGER_SAMPLES
#define DVC1124_SAFE_REINIT_TRIGGER_SAMPLES          3u
#endif
#ifndef DVC1124_SAFE_REINIT_COOLDOWN_SAMPLES
#define DVC1124_SAFE_REINIT_COOLDOWN_SAMPLES         25u /* 5 s at 200 ms */
#endif
#ifndef DVC1124_SAFE_VALID_RELEASE_SAMPLES
#define DVC1124_SAFE_VALID_RELEASE_SAMPLES           3u
#endif
#ifndef DVC1124_SHORT_AUTO_RECOVERY_ENABLE
#define DVC1124_SHORT_AUTO_RECOVERY_ENABLE           0u
#endif
#ifndef DVC1124_SHORT_CLEAR_VALID_SAMPLES
#define DVC1124_SHORT_CLEAR_VALID_SAMPLES            10u /* 2 s at 200 ms */
#endif
#ifndef DVC1124_SHORT_CLEAR_CURRENT_MA
#define DVC1124_SHORT_CLEAR_CURRENT_MA               200u
#endif

static dvc1124_safe_bms_status_t s_safe;

static uint8_t dvc_safe_u8_inc_sat(uint8_t value)
{
    return (value == 0xFFu) ? value : (uint8_t)(value + 1u);
}

static uint8_t dvc_safe_effective_fets(uint8_t *charge_on,
                                       uint8_t *discharge_on)
{
    dvc1124_snapshot_t snapshot;
    uint8_t chg;
    uint8_t dsg;

    if ((charge_on == 0) || (discharge_on == 0)) return 0u;

    chg = s_safe.requested_charge_on ? 1u : 0u;
    dsg = s_safe.requested_discharge_on ? 1u : 0u;
    DVC1124_GetSnapshot(&snapshot);

    if (!s_safe.output_enabled || s_safe.output_inhibit || !snapshot.valid)
    {
        chg = 0u;
        dsg = 0u;
    }
    if (s_safe.short_latched)
    {
        dsg = 0u;
    }

    *charge_on = chg;
    *discharge_on = dsg;
    return 1u;
}

static uint8_t dvc_safe_apply_requested_fets(void)
{
    uint8_t charge_on;
    uint8_t discharge_on;

    if (!dvc_safe_effective_fets(&charge_on, &discharge_on)) return 0u;

    /*
     * Call the legacy DVC adapter deliberately. Because dvc1124.h is included
     * before bms_afe.h in this translation unit, the compile-time alias in
     * bms_afe.h is suppressed and this resolves to the device-backed adapter.
     * That adapter still applies all existing directional BMS protection gates.
     */
    if (!bms_afe_set_fets(charge_on, discharge_on))
    {
        s_safe.output_inhibit = 1u;
        s_safe.snapshot_valid_streak = 0u;
        return 0u;
    }
    return 1u;
}

static void dvc_safe_note_invalid_snapshot(void)
{
    s_safe.output_inhibit = 1u;
    s_safe.snapshot_valid_streak = 0u;
    s_safe.short_clear_samples = 0u;
    s_safe.comm_failure_count = dvc_safe_u8_inc_sat(s_safe.comm_failure_count);

    /* Best-effort fail-safe command. A broken bus may prevent the write; the
     * inhibit remains latched regardless and no reopen request is issued. */
    (void)bms_afe_set_fets(0u, 0u);

    if (s_safe.reinit_cooldown_samples != 0u)
    {
        --s_safe.reinit_cooldown_samples;
    }

    if ((s_safe.comm_failure_count >= DVC1124_SAFE_REINIT_TRIGGER_SAMPLES) &&
        (s_safe.reinit_cooldown_samples == 0u))
    {
        /* Existing DVC init already performs register reset, project config,
         * protection apply and persistent DVC configuration restore. */
        bms_afe_init();
        s_safe.comm_failure_count = 0u;
        s_safe.reinit_cooldown_samples = DVC1124_SAFE_REINIT_COOLDOWN_SAMPLES;
    }
}

static void dvc_safe_update_short_latch(const dvc1124_snapshot_t *snapshot)
{
    if (snapshot == 0) return;

    if ((snapshot->alarm & DVC1124_ALARM_SCD_MASK) != 0u)
    {
        s_safe.short_latched = 1u;
        s_safe.short_clear_samples = 0u;
        return;
    }

#if DVC1124_SHORT_AUTO_RECOVERY_ENABLE
    if (s_safe.short_latched)
    {
        uint32_t abs_current_ma;

        if (snapshot->current_ma < 0)
            abs_current_ma = (uint32_t)(-snapshot->current_ma);
        else
            abs_current_ma = (uint32_t)snapshot->current_ma;

        /*
         * Auto recovery is intentionally disabled by default. The V1.2/V1.1
         * sources define the SCD flag and W0C clear operation but the currently
         * available D008 evidence does not prove a board-level load-removal
         * signal. If a product later enables this path, require both a clear
         * AFE alarm and sustained near-zero current before releasing software.
         */
        if ((abs_current_ma <= DVC1124_SHORT_CLEAR_CURRENT_MA) &&
            !bms_error_get(BMS_ERROR_CBC_DSG))
        {
            if (s_safe.short_clear_samples < DVC1124_SHORT_CLEAR_VALID_SAMPLES)
                ++s_safe.short_clear_samples;
            if (s_safe.short_clear_samples >= DVC1124_SHORT_CLEAR_VALID_SAMPLES)
            {
                s_safe.short_latched = 0u;
                s_safe.short_clear_samples = 0u;
            }
        }
        else
        {
            s_safe.short_clear_samples = 0u;
        }
    }
#else
    (void)snapshot;
    /* Fail-safe policy: no automatic software SCD unlatch without a proven
     * D008 load-removal criterion. Power-cycle/re-init keeps outputs inhibited
     * until fresh valid snapshots, while the existing DVC alarm path remains
     * responsible for the hardware W0C operation. */
#endif
}

void dvc1124_safe_bms_afe_init(void)
{
    memset(&s_safe, 0, sizeof(s_safe));
    s_safe.output_inhibit = 1u;
    bms_afe_init();
}

void dvc1124_safe_bms_afe_sample(void)
{
    dvc1124_snapshot_t snapshot;

    bms_afe_sample();
    DVC1124_GetSnapshot(&snapshot);

    if (!snapshot.valid)
    {
        dvc_safe_note_invalid_snapshot();
        return;
    }

    s_safe.comm_failure_count = 0u;
    if (s_safe.reinit_cooldown_samples != 0u)
        --s_safe.reinit_cooldown_samples;

    dvc_safe_update_short_latch(&snapshot);

    if (s_safe.snapshot_valid_streak < DVC1124_SAFE_VALID_RELEASE_SAMPLES)
        ++s_safe.snapshot_valid_streak;
    if (s_safe.snapshot_valid_streak >= DVC1124_SAFE_VALID_RELEASE_SAMPLES)
        s_safe.output_inhibit = 0u;

    (void)dvc_safe_apply_requested_fets();
}

void dvc1124_safe_bms_afe_sleep(void)
{
    /* Preserve the existing D008 MOS/sleep semantics. Do not invent a D013
     * shutdown policy for DVC1124; the legacy adapter issues CST=1110 only. */
    bms_afe_sleep();
}

uint8_t dvc1124_safe_bms_afe_apply_protection_config(void)
{
    uint8_t ok = bms_afe_apply_protection_config();

    if (!ok)
    {
        s_safe.output_inhibit = 1u;
        s_safe.snapshot_valid_streak = 0u;
        (void)bms_afe_set_fets(0u, 0u);
    }
    return ok;
}

uint8_t dvc1124_safe_bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    s_safe.requested_charge_on = charge_on ? 1u : 0u;
    s_safe.requested_discharge_on = discharge_on ? 1u : 0u;
    return dvc_safe_apply_requested_fets();
}

void dvc1124_safe_bms_afe_set_output_enabled(uint8_t enabled)
{
    s_safe.output_enabled = enabled ? 1u : 0u;
    bms_afe_set_output_enabled(s_safe.output_enabled);
    (void)dvc_safe_apply_requested_fets();
}

uint8_t dvc1124_safe_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurements)
{
    return bms_afe_get_aux_measurements(measurements);
}

void DVC1124_SafeBmsGetStatus(dvc1124_safe_bms_status_t *status)
{
    if (status != 0) *status = s_safe;
}
