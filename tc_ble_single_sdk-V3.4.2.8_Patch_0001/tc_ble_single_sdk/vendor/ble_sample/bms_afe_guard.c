#include "bms_afe.h"
#include "bms_features.h"
#include "bms_error.h"
#include <string.h>

#define BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT 3u
#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u

/*
 * bms_afe_sample() runs every 200 ms.
 *
 * Communication-loss safety is deliberately split into two layers:
 *   1. MCU software immediately removes output authorization and makes one
 *      best-effort OFF request while the bus may still be usable.
 *   2. After repeated failures the MCU stops ALL AFE bus traffic so the AFE's
 *      own hardware watchdog can expire and enforce the final MOS shutdown.
 *
 * DVC1124 production policy uses a 4 s I2C watchdog. Wait 5 s before one
 * recovery probe so failed MCU retries cannot keep feeding that watchdog.
 * SH36735xx production policy uses about 32 s; wait 35 s there.
 */
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 25u  /* 5 s */
#else
#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 175u /* 35 s */
#endif

typedef struct
{
    uint8_t requested_charge_on;
    uint8_t requested_discharge_on;
    uint8_t output_enabled;
    uint8_t comm_inhibit;
    uint8_t valid_snapshot_streak;
    uint8_t comm_failures;
    uint8_t bus_silenced;
    uint16_t failsafe_wait_samples;
} bms_afe_guard_state_t;

static bms_afe_guard_state_t s_guard;

uint8_t bms_afe_bus_access_allowed(void)
{
    return s_guard.bus_silenced ? 0u : 1u;
}

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
#define AFE_INIT() dvc1124_backend_init()
#define AFE_SAMPLE() dvc1124_backend_sample()
#define AFE_SLEEP() dvc1124_backend_sleep()
#define AFE_APPLY() dvc1124_backend_apply_protection_config()
#define AFE_FETS(c,d) dvc1124_backend_set_fets((c),(d))
#define AFE_OUTPUT(e) dvc1124_backend_set_output_enabled((e))
#define AFE_AUX(m) dvc1124_backend_get_aux_measurements((m))
#define AFE_FEATURE(s) dvc1124_backend_get_feature_snapshot((s))
#define AFE_CHARGER(p) dvc1124_backend_get_charge_source_present((p))
#define AFE_BAL_SET(m) dvc1124_backend_set_balance_mask((m))
#define AFE_BAL_GET(m) dvc1124_backend_get_balance_mask((m))
#define AFE_OW_START() dvc1124_backend_openwire_start()
#define AFE_OW_POLL(r) dvc1124_backend_openwire_poll((r))
#else
#define AFE_INIT() sh3673510_bms_afe_init()
#define AFE_SAMPLE() sh3673510_bms_afe_sample()
#define AFE_SLEEP() sh3673510_bms_afe_sleep()
#define AFE_APPLY() sh3673510_bms_afe_apply_protection_config()
#define AFE_FETS(c,d) sh3673510_bms_afe_set_fets((c),(d))
#define AFE_OUTPUT(e) sh3673510_bms_afe_set_output_enabled((e))
#define AFE_AUX(m) sh3673510_bms_afe_get_aux_measurements((m))
#define AFE_FEATURE(s) sh3673510_backend_get_feature_snapshot((s))
#define AFE_CHARGER(p) sh3673510_backend_get_charge_source_present((p))
#define AFE_BAL_SET(m) sh3673510_backend_set_balance_mask((m))
#define AFE_BAL_GET(m) sh3673510_backend_get_balance_mask((m))
#define AFE_OW_START() sh3673510_backend_openwire_start()
#define AFE_OW_POLL(r) sh3673510_backend_openwire_poll((r))
#endif

static void inhibit_local(void)
{
    s_guard.comm_inhibit = 1u;
    s_guard.valid_snapshot_streak = 0u;
    bms_features_on_afe_invalid();
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
}

static void best_effort_shutdown(void)
{
    if (s_guard.bus_silenced) return;

    /*
     * This is NOT the communication-loss safety guarantee. It is only a final
     * software attempt while the bus may still accept commands. If the bus is
     * already dead, the hardware AFE watchdog is the authoritative shutdown.
     */
    (void)AFE_BAL_SET(0u);
    (void)AFE_FETS(0u, 0u);
}

static void enter_failsafe_wait(void)
{
    inhibit_local();
    s_guard.bus_silenced = 1u;
    s_guard.failsafe_wait_samples = BMS_AFE_FAILSAFE_WAIT_SAMPLES;
    s_guard.comm_failures = 0u;
}

static uint8_t service_failsafe_wait(void)
{
    if (!s_guard.bus_silenced) return 0u;

    /* Absolutely no AFE I2C/SPI access while the hardware watchdog is timing. */
    if (s_guard.failsafe_wait_samples != 0u)
    {
        --s_guard.failsafe_wait_samples;
        if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
        return 1u;
    }

    /* One bounded recovery attempt after the watchdog window has elapsed. */
    s_guard.bus_silenced = 0u;
    s_guard.comm_failures = 0u;
    s_guard.valid_snapshot_streak = 0u;
    s_guard.comm_inhibit = 1u;

    AFE_INIT();
    AFE_OUTPUT(s_guard.output_enabled);

    /* Backend init may clear its own communication error; qualification has
     * not happened yet, so keep the system-level AFE fault asserted. */
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}

static uint8_t apply_requested(void)
{
    uint8_t c;
    uint8_t d;

    /* During communication inhibit the MCU owns authorization only; it must
     * not repeatedly write OFF commands and accidentally feed the AFE WDT. */
    if (s_guard.comm_inhibit || s_guard.bus_silenced) return 1u;

    c = s_guard.requested_charge_on;
    d = s_guard.requested_discharge_on;
    if (!s_guard.output_enabled)
    {
        c = 0u;
        d = 0u;
    }
    if (bms_features_charge_blocked()) c = 0u;
    if (bms_features_discharge_blocked()) d = 0u;
    return AFE_FETS(c, d);
}

static void note_invalid(void)
{
    inhibit_local();

    /* One best-effort controlled shutdown only. Repeated writes after a dead
     * bus are both ineffective and can prevent a marginal AFE WDT from firing. */
    if (s_guard.comm_failures == 0u) best_effort_shutdown();

    if (s_guard.comm_failures != 0xFFu) ++s_guard.comm_failures;
    if (s_guard.comm_failures >= BMS_AFE_COMM_FAILS_BEFORE_SILENCE)
        enter_failsafe_wait();
}

void bms_afe_init(void)
{
    memset(&s_guard, 0, sizeof(s_guard));
    s_guard.comm_inhibit = 1u;
    AFE_INIT();
    AFE_OUTPUT(0u);
    (void)AFE_FETS(0u, 0u);
    bms_features_init();
    (void)AFE_BAL_SET(0u);
}

void bms_afe_sample(void)
{
    bms_afe_aux_measurements_t m;

    if (service_failsafe_wait()) return;

    AFE_SAMPLE();
    memset(&m, 0, sizeof(m));
    if (!AFE_AUX(&m))
    {
        note_invalid();
        return;
    }

    s_guard.comm_failures = 0u;
    if (s_guard.valid_snapshot_streak < BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
        ++s_guard.valid_snapshot_streak;

    if (s_guard.valid_snapshot_streak >= BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
    {
        s_guard.comm_inhibit = 0u;
    }
    else
    {
        /* A single recovered frame is not enough to re-authorize outputs. */
        if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    }

    bms_features_service();
    if (!apply_requested()) note_invalid();
}

void bms_afe_sleep(void)
{
    inhibit_local();
    s_guard.comm_failures = 0u;

    /* If the bus is intentionally silent after a communication fault, do not
     * touch it again before MCU deep sleep. The AFE watchdog owns MOS safety. */
    if (s_guard.bus_silenced) return;

    best_effort_shutdown();
    AFE_SLEEP();
}

uint8_t bms_afe_apply_protection_config(void)
{
    uint8_t ok;

    if (s_guard.comm_inhibit || s_guard.bus_silenced) return 0u;
    ok = AFE_APPLY();
    if (!ok) note_invalid();
    return ok;
}

uint8_t bms_afe_set_fets(uint8_t c, uint8_t d)
{
    uint8_t requested_c = c ? 1u : 0u;
    uint8_t requested_d = d ? 1u : 0u;

    if (s_guard.requested_charge_on == requested_c &&
        s_guard.requested_discharge_on == requested_d)
    {
        return 1u;
    }

    s_guard.requested_charge_on = requested_c;
    s_guard.requested_discharge_on = requested_d;

    /* Cache the product request while communication is unqualified. Never use
     * a requested state change as a reason to touch a silenced AFE bus. */
    if (s_guard.comm_inhibit || s_guard.bus_silenced) return 1u;

    if (!apply_requested())
    {
        note_invalid();
        return 0u;
    }
    return 1u;
}

void bms_afe_get_requested_fets(uint8_t *charge_on, uint8_t *discharge_on)
{
    if (charge_on != 0) *charge_on = s_guard.requested_charge_on;
    if (discharge_on != 0) *discharge_on = s_guard.requested_discharge_on;
}

void bms_afe_set_output_enabled(uint8_t e)
{
    s_guard.output_enabled = e ? 1u : 0u;

    if (!s_guard.bus_silenced)
        AFE_OUTPUT(s_guard.output_enabled);

    if (!s_guard.output_enabled)
    {
        bms_features_on_afe_invalid();
        if (!s_guard.bus_silenced) best_effort_shutdown();
        return;
    }

    if (s_guard.comm_inhibit || s_guard.bus_silenced) return;
    if (!apply_requested()) note_invalid();
}

uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m)
{
    if (!m) return 0u;
    if (s_guard.comm_inhibit || s_guard.bus_silenced)
    {
        memset(m, 0, sizeof(*m));
        return 0u;
    }
    if (!AFE_AUX(m))
    {
        memset(m, 0, sizeof(*m));
        return 0u;
    }
    return 1u;
}

uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s)
{
    if (!s || s_guard.comm_inhibit || s_guard.bus_silenced) return 0u;
    return AFE_FEATURE(s);
}

uint8_t bms_afe_get_charge_source_present(uint8_t *p)
{
    if (!p || s_guard.comm_inhibit || s_guard.bus_silenced) return 0u;
    return AFE_CHARGER(p);
}

uint8_t bms_afe_set_balance_mask(uint32_t m)
{
    if (s_guard.comm_inhibit || s_guard.bus_silenced)
        return (m == 0u) ? 1u : 0u;
    return AFE_BAL_SET(m);
}

uint8_t bms_afe_get_balance_mask(uint32_t *m)
{
    if (!m) return 0u;
    if (s_guard.comm_inhibit || s_guard.bus_silenced)
    {
        *m = 0u;
        return 0u;
    }
    return AFE_BAL_GET(m);
}

uint8_t bms_afe_openwire_start(void)
{
    if (s_guard.comm_inhibit || s_guard.bus_silenced) return 0u;
    (void)AFE_BAL_SET(0u);
    if (!AFE_FETS(0u, 0u)) return 0u;
    return AFE_OW_START();
}

bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *r)
{
    if (s_guard.comm_inhibit || s_guard.bus_silenced)
        return BMS_AFE_DIAG_ERROR;
    return AFE_OW_POLL(r);
}
