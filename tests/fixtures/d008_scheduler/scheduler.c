#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "bms_afe.h"
#include "bms_types.h"

#undef LED_BLUE_PIN

#define _attribute_no_inline_
#define APP_SAMPLE_PERIOD_US 200000u
#define SYSTEM_TIMER_TICK_1US 16u
#define SUSPEND_DISABLE 0u
#define LED_BLUE_PIN 1u
static uint32_t now, s_sample_tick, deadline;
static uint8_t wake, valid = 1u;
static volatile uint8_t s_sample_due;
static char trace[80];
static unsigned used;

static void note(char step) { trace[used++] = step; trace[used] = 0; }
static uint32_t clock_time(void) { return now; }
static int clock_time_exceed(uint32_t tick, uint32_t us)
{ return (uint32_t)(now - tick) > us * SYSTEM_TIMER_TICK_1US; }
static void bls_pm_setAppWakeupLowPower(uint32_t tick, uint8_t enabled)
{ deadline = tick; wake = enabled; }
static void bls_pm_setSuspendMask(uint8_t flags) { assert(!flags); note('D'); }
static void app_acc_sleep_hold(void) { note('H'); }
static void blt_sdk_main_loop(void) { note('B'); }
static void bus_mux_task(void) { note('X'); }
static void sif_prepare_task(uint32_t tick) { assert(tick == s_sample_tick); note('I'); }
static void main_loop_modbus(void) { note('U'); }
static void app_storage_task(void) { note('F'); }
static void app_power_process(uint8_t sampled)
{ assert(sampled == (strchr(trace, 'A') != 0)); note('P'); }
static void gpio_toggle(unsigned pin) { assert(pin == LED_BLUE_PIN); note('L'); }
void bms_features_service(void) { note('T'); }
void bms_afe_apply_outputs(void) { note('M'); }
bms_afe_sample_status_t bms_afe_sample(void)
{ note('A'); return valid ? BMS_AFE_SAMPLE_OK : BMS_AFE_SAMPLE_READ_ERROR; }
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *measurement)
{ measurement->current_ma = -1000; return valid; }
static void bms_soc_app_sample(const bms_afe_aux_measurements_t *sample)
{ assert(valid ? sample && sample->current_ma == -1000 : !sample); note('S'); }
static void bms_event_log_observe(void) { note('E'); }

/* PRODUCTION */

static void expect(const char *order) { assert(!strcmp(trace, order)); used = 0u; trace[0] = 0; }
int main(void)
{
    s_sample_tick = UINT32_MAX - 99u;
    now = s_sample_tick;
    app_schedule_sample_wakeup();
    assert(wake && deadline == (uint32_t)(s_sample_tick + 3200000u));
    now = s_sample_tick + 3200000u;
    main_loop(); expect("BXXIUFP"); assert(!app_sample_is_due());
    ++now;
    main_loop(); expect("DBXATMSELXIUFP"); assert(!app_sample_is_due());
    valid = 0u;
    s_sample_due = 1u;
    main_loop(); expect("DBXASELXIUFP");
    s_power_state = APP_POWER_ACC_SLEEP;
    main_loop(); expect("H");
    s_power_state = APP_POWER_PROTECTIVE_SHUTDOWN;
    main_loop(); expect("");
    return 0;
}
