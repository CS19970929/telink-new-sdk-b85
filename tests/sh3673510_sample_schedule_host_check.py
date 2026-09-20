"""Execute the SH production sample scheduler, mocking only time/SDK/consumers."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
source = (APP / 'app.c').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'(?m)^static void ' + name + r'\(', source)
    assert match, 'missing production scheduler: ' + name
    return source[match.start():source.index('\n}\n', match.start()) + 3]


assert 'bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup)' in source
assert '    app_sample_task();' in source
assert function('app_sample_task').count('bms_afe_sample();') == 1
assert 'test_task_tick' not in source and 'hello World!!!' not in source
period = re.search(r'(?m)^#define APP_SAMPLE_PERIOD_US\s+\d+u', source).group()
code = r'''
#include <stdint.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define SYSTEM_TIMER_TICK_1US 16u
#define MODE_FACTORY 1
typedef struct { int32_t current_ma; u32 sample_tick_32k; } bms_afe_aux_measurements_t;
static u32 s_sample_tick, fake_tick, scheduled_tick, sample_cost;
static volatile u8 s_sample_due;
static unsigned samples, soc_calls, mos_calls, diag_calls, failures;
static u8 valid, last_valid;
static int32_t last_current;
static u32 last_tick;
static u32 clock_time(void) { return fake_tick; }
static u32 pm_get_32k_tick(void) { return 4321; }
static int clock_time_exceed(u32 since, u32 us) { return (u32)(fake_tick-since) > us*16u; }
static void bls_pm_setAppWakeupLowPower(u32 tick, unsigned enable) {
    if (!enable) ++failures;
    scheduled_tick = tick;
}
static void bms_afe_sample(void) { ++samples; fake_tick += sample_cost; }
static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *s) {
    if (valid) { s->current_ma = 1234; s->sample_tick_32k = 123; }
    return valid;
}
static int32_t bms_afe_current_to_soc_ma(int32_t x) { return -x; }
static void APP_SOC_IntEnhance_Ctrl(u8 v, int32_t c, u32 t) {
    ++soc_calls; last_valid=v; last_current=c; last_tick=t;
}
static void mos_update(void) { ++mos_calls; }
static unsigned Runtime_GetMode(void) { return 0; }
static void bms_diag_poll_runtime(u8 v, int32_t c, u32 t, u8 factory) {
    (void)v; (void)c; (void)t; (void)factory; ++diag_calls;
}
/* PRODUCTION */
#define CHECK(c) do { if (!(c)) { ++failures; printf("FAIL %d: %s\n", __LINE__, #c); } } while(0)
int main(void) {
    valid=1; s_sample_tick=100; fake_tick=100;
    app_sample_task(); CHECK(samples==0);
    fake_tick += APP_SAMPLE_PERIOD_US*16u + 1;
    app_sample_task(); CHECK(samples==1 && soc_calls==1 && mos_calls==1 && diag_calls==1);
    CHECK(last_valid && last_current==-1234 && last_tick==123);
    CHECK(scheduled_tick==s_sample_tick+APP_SAMPLE_PERIOD_US*16u);
    app_sample_task(); CHECK(samples==1);
    app_sample_wakeup(0); app_sample_wakeup(0); CHECK(samples==1);
    app_sample_task(); CHECK(samples==2); /* multiple IRQ marks coalesce */
    valid=0; app_sample_wakeup(0); app_sample_task();
    CHECK(!last_valid && last_current==0 && last_tick==4321);
    sample_cost=APP_SAMPLE_PERIOD_US*16u+1; app_sample_wakeup(0);
    app_sample_task(); CHECK(samples==4 && s_sample_due); /* no catch-up loop */
    sample_cost=0; app_sample_task(); CHECK(samples==5 && !s_sample_due);
    s_sample_tick=UINT32_MAX-100; fake_tick=s_sample_tick+APP_SAMPLE_PERIOD_US*16u+1;
    app_sample_task(); CHECK(samples==6); /* timer wrap */
    printf("SH sample scheduler: %u failures\n", failures);
    return failures ? 1 : 0;
}
'''
body = period + '\n' + '\n'.join(function(n) for n in (
    'app_sample_wakeup', 'app_schedule_sample_wakeup', 'app_sample_task'))
with tempfile.TemporaryDirectory(prefix='sh3510-scheduler-') as tmp:
    c, exe = Path(tmp)/'check.c', Path(tmp)/'check.exe'
    c.write_text(code.replace('/* PRODUCTION */', body), encoding='utf-8')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                    '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
