"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sh3673510_sample_schedule_host_check():
    print("CHECK sh3673510_sample_schedule_host_check", flush=True)
    """Execute the SH production sample scheduler, mocking only time/SDK/consumers."""
    from validation_support import function as extract_function
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import os
    import re
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    source = selected_source(APP / 'app.c')


    def function(name):
        return extract_function(source, 'static void ' + name + '(')

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
    static void app_update_soc_from_sample(u8 v, int32_t c, u32 t) {
        ++soc_calls; last_valid=v; last_current=c; last_tick=t;
    }
    static void mos_update(void) { ++mos_calls; }
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
        CHECK(last_valid && last_current==1234 && last_tick==123);
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

def check_sh3673510_sleep_host_check():
    print("CHECK sh3673510_sleep_host_check", flush=True)
    """Run the production SH sleep call chain with injected bus and PM failures.

    Only peripheral outcomes are mocked. This proves software sequencing, not AFE
    sleep current, GPIO timing, or physical MOS shutdown. No project temp files.
    """
    from validation_support import function as extract_function
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import os
    import re
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)


    def function(file, name):
        source = (APP / file).read_text(encoding='utf-8')
        match = re.search(r'(?m)^(?:static )?(?:void|int|uint8_t|u32) ' + name + r'\(', source)
        assert match, name
        return extract_function(source, match.group(0))

    guard = (APP / 'bms_afe_guard.c').read_text(encoding='utf-8')
    state = re.search(r'^#define BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT .*$', guard, re.M).group(0) + '\n' + guard[guard.index('typedef struct'):guard.index('uint8_t bms_afe_bus_access_allowed')]
    body = '#include "bms_debug_log.h"\n' + state + '\n'.join([
        function('sh3673510_control.c', 'sh3673510_control_sleep'),
        function('sh3673510_control.c', 'sh3673510_control_wake'),
        function('sh3673510_bms.c', 'sh3673510_bms_afe_sleep'),
        function('bms_afe_guard.c', 'inhibit_local'),
        function('bms_afe_guard.c', 'best_effort_shutdown'),
        function('bms_afe_guard.c', 'enter_failsafe_wait'),
        function('bms_afe_guard.c', 'note_invalid'),
        function('bms_afe_guard.c', 'bms_afe_sleep'),
        function('app.c', 'app_note_sleep_and_enter_deepsleep'),
        function('app.c', 'app_pm_elapsed_limit'),
    ])
    app_source = selected_source(APP / 'app.c')
    for counter in ('sleep_cnt', 'sleep_veryvlow_cnt', 'sleep_vlow_cnt',
                    'sleep_vnormal_cnt', 'afe_comm_err_sleepcnt'):
        assert f'{counter} = app_pm_elapsed_limit(' in app_source
        assert f'if (app_note_sleep_and_enter_deepsleep(1u)) {counter} = 0;' in app_source
    fixture = '#define BMS_AFE_BACKEND 2\n#define BMS_AFE_BACKEND_DVC1124 1\n' + (ROOT / 'tests/fixtures/sh3673510_sleep.c').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='sh3510-sleep-') as tmp:
        c, exe = Path(tmp) / 'check.c', Path(tmp) / 'check.exe'
        c.write_text(fixture.replace('/* PRODUCTION */', body), encoding='utf-8')
        # Default 1 reproduces the actual fixed-UART gate. Test-only 0 retains
        # fault-injection coverage of the latent PM body; it is not a product mode.
        config = (ROOT / 'bms/products/sh3673510_defaults.h').read_text(encoding='utf-8')
        assert re.search(r'^#define SH3673510_FIXED_UART_BLOCKS_PM 1u$', config, re.M)
        for blocked in (1, 0):
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                            '-DSH3673510_FIXED_UART_BLOCKS_PM=%d' % blocked,
                            *host_includes(ROOT), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    print('SH production sleep/control/backend/guard/app fault injection: PASS')

if __name__ == "__main__":
    check_sh3673510_sample_schedule_host_check()
    check_sh3673510_sleep_host_check()
