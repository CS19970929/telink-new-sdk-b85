#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sh3673520_reg.h"
typedef uint8_t u8;
typedef uint32_t u32;
enum { SH3673520_OK, SH3673520_ERR_SPI };
enum { BMS_ERROR_AFE1, BUS_STATE_OWC_IDLE = 0 };
enum { DEEPSLEEP_MODE = 1, PM_WAKEUP_PAD = 1, STATUS_GPIO_ERR_NO_ENTER_PM = 256 };
#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE 2u
#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 175u
static uint8_t s_control_ready, s_afe_sleeping;
static uint8_t s_short_clear_pending, s_output_inhibit, s_valid_snapshot_streak;
static uint8_t s_fet_command_valid, s_snapshot_valid;
static uint16_t s_short_release_count, s_balance_mask, s_hw_recovery_count[9];
static unsigned errors, invalidations, comm_errors, failures, assertions;
static unsigned bus_calls, balance_calls, fet_calls, sleep_writes, normal_writes;
static unsigned pm_calls, event_calls, prepare_calls, cancel_calls;
static unsigned wake_calls, wake_on_call, command_fail_at;
static uint8_t ota_is_working, bus_busy, tx_busy, flash_locked;
static uint8_t control_wake_active, heater_on, physical_sleep, last_c, last_d;
static uint8_t configure_ok, protection_ok;
static int pm_status;
static u32 fake_tick;
static uint8_t io_ok(void) { ++bus_calls; return bus_calls != command_fail_at; }
static uint8_t sh3673510_board_wake_active(void) { return control_wake_active; }
static uint8_t sh3673510_control_set_balance(uint16_t mask) {
    ++balance_calls; (void)mask; return io_ok();
}
static uint8_t sh3673510_control_set_fets(uint8_t c, uint8_t d) {
    ++fet_calls;
    if (!io_ok()) return 0;
    last_c = c; last_d = d; return 1;
}
static void sh3673510_board_set_heater(uint8_t on) { heater_on = on; }
static void sh3673510_board_force_heater_fuse_safe(void) {}
static uint8_t sh3510_update_reg(uint8_t reg, uint8_t mask, uint8_t val) {
    (void)reg; (void)mask; (void)val; return io_ok();
}
static int SH3673520_WriteReg(uint8_t reg, uint8_t val) {
    (void)reg;
    /* Lost ACK: the AFE may accept the command even when the MCU sees failure. */
    if (val == SH3673520_SCONF1_SLEEP) { ++sleep_writes; physical_sleep = 1; }
    else { ++normal_writes; physical_sleep = 0; }
    return io_ok() ? SH3673520_OK : SH3673520_ERR_SPI;
}
static void sh3673520_port_delay_ms(unsigned ms) { (void)ms; }
static uint8_t sh3510_configure_runtime(void) { return configure_ok; }
static uint8_t sh3673510_control_apply_protection(void) { return protection_ok; }
static void note_comm_error(void) { ++comm_errors; }
static uint8_t bms_error_get(unsigned e) { (void)e; return errors != 0; }
static void bms_error_raise(unsigned e) { (void)e; ++errors; }
static void bms_features_on_afe_invalid(void) { ++invalidations; heater_on = 0; }
static int app_deepsleep_pad_wakeup_active(void) {
    ++wake_calls; return wake_on_call && wake_calls >= wake_on_call;
}
static int bus_mux_get_state(void) { return bus_busy; }
static int uart_tx_is_busy(void) { return tx_busy; }
static int app_flash_lock_restore_enabled(void) { return flash_locked; }
static u32 pm_get_32k_tick(void) { return fake_tick; }
static void bms_event_log_note_sleep(void) { ++event_calls; }
static void Runtime_PrepareForDeepSleep(void) { ++prepare_calls; }
static void Runtime_CancelPendingDeepSleep(void) { ++cancel_calls; }
static int cpu_sleep_wakeup(unsigned mode, unsigned source, unsigned tick) {
    (void)mode; (void)source; (void)tick; ++pm_calls; return pm_status;
}
#define AFE_SLEEP() sh3673510_bms_afe_sleep()
#define AFE_BAL_SET(m) sh3673510_control_set_balance(m)
#define AFE_FETS(c,d) sh3673510_control_set_fets(c,d)
#define APP_PM_TICKS_PER_SEC 32000u

/* PRODUCTION */

#define CHECK(c) do { ++assertions; if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static void reset(void) {
    /* Advance past any retry deadline, including across uint32 wrap. */
    fake_tick += 4u * APP_PM_TICKS_PER_SEC;
    memset(&s_guard, 0, sizeof(s_guard));
    s_guard.output_enabled = 1;
    s_guard.valid_snapshot_streak = 3;
    s_control_ready = 1; s_afe_sleeping = physical_sleep = 0;
    s_snapshot_valid = s_fet_command_valid = 1;
    s_output_inhibit = 0; s_valid_snapshot_streak = 3;
    s_short_clear_pending = 1; s_short_release_count = 8;
    s_balance_mask = 3; memset(s_hw_recovery_count, 1, sizeof(s_hw_recovery_count));
    errors = invalidations = comm_errors = bus_calls = 0;
    balance_calls = fet_calls = sleep_writes = normal_writes = 0;
    pm_calls = event_calls = prepare_calls = cancel_calls = 0;
    wake_calls = wake_on_call = command_fail_at = 0;
    ota_is_working = bus_busy = tx_busy = control_wake_active = 0;
    configure_ok = protection_ok = flash_locked = 1;
    heater_on = last_c = last_d = 1; pm_status = 0;
}
int main(void) {
    unsigned i, n;
    reset(); ota_is_working = 1;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !bus_calls);
    reset(); bus_busy = 1;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !bus_calls);
    reset(); tx_busy = 1;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !bus_calls);
    reset(); flash_locked = 0;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !bus_calls);
    reset(); wake_on_call = 1;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !bus_calls);

    reset(); s_control_ready = 0;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !sleep_writes);
    reset(); control_wake_active = 1;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls && !sleep_writes);

    reset(); s_guard.bus_silenced = 1; s_guard.failsafe_wait_samples = 90;
    s_guard.comm_failures = 1;
    CHECK(!app_note_sleep_and_enter_deepsleep(1));
    CHECK(!bus_calls && !pm_calls && s_guard.failsafe_wait_samples == 90);
    CHECK(s_guard.comm_failures == 1);

    reset(); s_control_ready = 0;
    CHECK(!bms_afe_sleep()); CHECK(s_guard.comm_failures == 1);
    CHECK(!bms_afe_sleep()); CHECK(s_guard.bus_silenced);
    n = bus_calls;
    CHECK(!bms_afe_sleep()); CHECK(bus_calls == n);
    CHECK(s_guard.failsafe_wait_samples == BMS_AFE_FAILSAFE_WAIT_SAMPLES);

    /* Learn the successful path's IO count; fail every operation in turn. */
    reset(); CHECK(app_note_sleep_and_enter_deepsleep(1)); n = bus_calls;
    CHECK(pm_calls == 1 && sleep_writes == 1 && event_calls == 1);
    CHECK(!heater_on && !last_c && !last_d);
    CHECK(!s_snapshot_valid && !s_fet_command_valid && s_output_inhibit);
    for (i = 1; i <= n; ++i) {
        reset(); command_fail_at = i;
        CHECK(!app_note_sleep_and_enter_deepsleep(1));
        CHECK(!pm_calls && !prepare_calls && !event_calls);
        CHECK(!s_snapshot_valid && !s_fet_command_valid && s_output_inhibit);
    }

    reset(); wake_on_call = 2;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(!pm_calls);
    CHECK(sh3673510_control_wake()); CHECK(!physical_sleep);

    reset(); pm_status = STATUS_GPIO_ERR_NO_ENTER_PM;
    CHECK(!app_note_sleep_and_enter_deepsleep(1));
    CHECK(prepare_calls == 1 && cancel_calls == 1);
    CHECK(sh3673510_control_wake()); CHECK(!physical_sleep);

    /* Failed SLEEP acknowledgement must still force NORMAL/configure on resume. */
    reset(); command_fail_at = n;
    CHECK(!app_note_sleep_and_enter_deepsleep(1));
    CHECK(sh3673510_control_wake());
    CHECK(normal_writes == 1 && !physical_sleep);

    reset(); CHECK(app_note_sleep_and_enter_deepsleep(1)); configure_ok = 0;
    CHECK(!sh3673510_control_wake() && s_afe_sleeping);
    configure_ok = 1; protection_ok = 0;
    CHECK(!sh3673510_control_wake() && s_afe_sleeping);
    protection_ok = 1; CHECK(sh3673510_control_wake() && !s_afe_sleeping);

    /* Same PM failure is retried at a bounded cadence, including tick wrap. */
    reset(); fake_tick = UINT32_MAX - APP_PM_TICKS_PER_SEC;
    pm_status = STATUS_GPIO_ERR_NO_ENTER_PM;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); n = bus_calls;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(bus_calls == n && pm_calls == 1);
    fake_tick += 3u * APP_PM_TICKS_PER_SEC;
    CHECK(!app_note_sleep_and_enter_deepsleep(1)); CHECK(pm_calls == 2);

    CHECK(app_pm_elapsed_limit(3599, 1, 3600) == 3600);
    CHECK(app_pm_elapsed_limit(3600, UINT32_MAX, 3600) == 3600);
    CHECK(app_pm_elapsed_limit(0, UINT32_MAX, 3600) == 3600);
    CHECK(app_pm_elapsed_limit(7, 12, 3600) == 19);
    CHECK(app_pm_elapsed_limit(UINT32_MAX - 1, 1, UINT32_MAX) == UINT32_MAX);
    CHECK(app_pm_elapsed_limit(0, 0, 0) == 0);
    printf("SH sleep: %u assertions, %u failures\n", assertions, failures);
    return failures ? 1 : 0;
}
