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
    period = (APP/'bms_timing.h').read_text() + '\n' + re.search(
        r'(?m)^#define APP_SAMPLE_PERIOD_US[^\n]*', (APP/'app.h').read_text()).group()
    code = r'''
    #include <stdint.h>
    #include <stdio.h>
    typedef uint8_t u8;
    typedef uint32_t u32;
    #define SYSTEM_TIMER_TICK_1US 16u
    #define MODE_FACTORY 1
    typedef struct { int32_t raw_current_ma; int32_t current_ma; u32 sample_tick_32k; u8 sample_fresh; } bms_afe_aux_measurements_t;
    static u32 s_sample_tick, fake_tick, scheduled_tick, sample_cost;
    static volatile u8 s_sample_due;
    static unsigned samples, soc_calls, diag_calls, failures;
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
        if (valid) { s->raw_current_ma = 1230; s->current_ma = 1234; s->sample_tick_32k = 123; s->sample_fresh = 1; }
        return valid;
    }
    static void app_update_soc_from_sample(u8 v, int32_t c, u8 t) {
        ++soc_calls; last_valid=v; last_current=c; last_tick=t;
    }
    static void bms_diag_poll_runtime(u8 v, int32_t raw, int32_t c, u32 t) {
        (void)v; (void)c; (void)t; (void)raw; ++diag_calls;
    }
    /* PRODUCTION */
    #define CHECK(c) do { if (!(c)) { ++failures; printf("FAIL %d: %s\n", __LINE__, #c); } } while(0)
    int main(void) {
        valid=1; s_sample_tick=100; fake_tick=100;
        app_sample_task(); CHECK(samples==0);
        fake_tick += APP_SAMPLE_PERIOD_US*16u + 1;
        app_sample_task(); CHECK(samples==1 && soc_calls==1 && diag_calls==1);
        CHECK(last_valid && last_current==1234 && last_tick==1);
        CHECK(scheduled_tick==s_sample_tick+APP_SAMPLE_PERIOD_US*16u);
        app_sample_task(); CHECK(samples==1);
        app_sample_wakeup(0); app_sample_wakeup(0); CHECK(samples==1);
        app_sample_task(); CHECK(samples==2); /* multiple IRQ marks coalesce */
        valid=0; app_sample_wakeup(0); app_sample_task();
        CHECK(!last_valid && last_current==0 && last_tick==0);
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
        function('app_power.c', 'app_enter_switch_deepsleep'),
        function('app_power.c', 'app_pm_elapsed_limit'),
    ])
    app_source = selected_source(APP / 'app_power.c')
    assert 'sleep_cnt = app_pm_elapsed_limit(' in app_source
    assert 'if (app_enter_switch_deepsleep()) sleep_cnt = 0;' in app_source
    assert 'if (app_protective_sleep_poll(sleep_elapsed_sec)) return;' in app_source
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

def check_sh_protective_sleep():
    """执行当前 SH 产品的保护计时/深睡保持生产函数，边界均注入失败。"""
    from pathlib import Path
    from project_paths import selected_source
    from validation_support import function, run_c
    root = Path(__file__).resolve().parents[1]
    source = selected_source(root / 'bms/app/app_power.c')
    start = source.index('#define APP_AFE_ERROR_SLEEP_SECONDS')
    end = source.index('static app_protective_sleep_t s_protective_sleep;', start)
    body = source[start:end] + 'static app_protective_sleep_t s_protective_sleep;\n'
    body += '\n'.join(function(source, signature) for signature in (
        'static uint8_t app_get_fresh_measurements(',
        'static u32 app_pm_take_elapsed_seconds(', 'static u32 app_pm_elapsed_limit(',
        'static void app_protective_wakeup_pin(', 'static void app_protective_sleep_hold(',
        'static void app_enter_protective_sleep(', 'static u8 app_protective_sleep_poll(',
        'uint8_t app_power_prepare_loop('))
    fixture = (root / 'tests/fixtures/protective_sleep.c').read_text(encoding='utf-8')
    run_c(fixture.replace('/* PRODUCTION */', body), name='sh-protective-sleep')

def check_sh_suspend_current_gates():
    """运行真实 SH 普通 suspend 入口；关闭固定门禁仅用于覆盖潜在分支。"""
    from pathlib import Path
    import re
    from project_paths import selected_source
    from validation_support import function, run_c
    root = Path(__file__).resolve().parents[1]
    source = selected_source(root / 'bms/app/app_power.c')
    defines = re.search(r'^#define APP_SUSPEND_EXIT_CURRENT_MA[^\n]*', source, re.M).group()
    defines += '\n' + re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                               (root / 'bms/core/bms_soc.h').read_text(), re.M).group()
    body = '\n'.join(function(source, signature) for signature in (
        'static uint8_t app_get_fresh_measurements(', 'void app_power_process('))
    fixture = r'''
    #include <stdint.h>
    #include <stdbool.h>
    #include <assert.h>
    typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
    typedef struct { int unused; } app_pm_elapsed_ctx_t;
    typedef struct { int32_t current_ma; u32 sample_tick_32k; } bms_afe_aux_measurements_t;
    #define BMS_SOC_MAX_SAMPLE_GAP_32K 12800u
    #define BMS_SAMPLE_MAX_POLL_GAP_32K (400u * 32u)
    #define BMS_BOARD_SWITCH_PIN 0
    #define BMS_BOARD_INT_WK_MCU_PIN 1
    #define Level_Low 0
    #define SUSPEND_DISABLE 0
    #define SUSPEND_ADV 1
    #define SUSPEND_CONN 2
    static bool s_low_power_mode;
    static int valid=1, flash_locked=1, tx_busy, modbus_busy, ota_is_working;
    static int device_in_connection_state, switch_high=1;
    static int mask, observed_allowed, mask_calls;
    static u32 now=100;
    static bms_afe_aux_measurements_t measurement={0,100};
    static u32 pm_get_32k_tick(void){return now;}
    static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m){*m=measurement;return valid;}
    static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t *c){(void)c;return 0;}
    static u8 app_protective_sleep_poll(u32 seconds){(void)seconds;return 0;}
    static void app_sh_publish_sleep(u32 seconds,u8 allowed){(void)seconds;observed_allowed=allowed;}
    static int board_switch_is_on(void){return 1;}
    static int gpio_read(int pin){return pin==BMS_BOARD_SWITCH_PIN?switch_high:1;}
    static void cpu_set_gpio_wakeup(int pin,int level,int enable){(void)pin;(void)level;(void)enable;}
    static u32 app_pm_elapsed_limit(u32 current,u32 elapsed,u32 limit){(void)elapsed;(void)limit;return current;}
    static int app_enter_switch_deepsleep(void){return 0;}
    static int uart_tx_is_busy(void){return tx_busy;}
    static int modbus_uart_tx_active(void){return modbus_busy;}
    static int app_flash_lock_restore_enabled(void){return flash_locked;}
    static void bls_pm_setSuspendMask(int value){mask=value;++mask_calls;}
    static void bls_pm_setManualLatency(int value){(void)value;}
    /* PRODUCTION */
    int main(void){
        volatile u8 due=0;
        int32_t currents[]={INT32_MIN,-500,-201,-200,-199,0,199,200,201,500,INT32_MAX};
        for(unsigned i=0;i<sizeof(currents)/sizeof(currents[0]);i++){
            measurement.current_ma=currents[i];app_power_process(&due);
            int blocked=SH3673510_FIXED_UART_BLOCKS_PM || currents[i]<=-200 || currents[i]>=200;
            assert(mask==(blocked?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
            assert(observed_allowed==!blocked);
        }
        measurement.current_ma=0;
        valid=0;app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        valid=1;now=measurement.sample_tick_32k+12801u;
        app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        now=100;due=1;app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        due=0;flash_locked=0;app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        /* 各门禁及 BLE 连接组合：控制输出与最终诊断必须一致，每轮只设置一次 mask。 */
        for(unsigned bits=0;bits<512u;bits++){
            switch_high=(bits&1u)!=0;flash_locked=(bits&2u)!=0;
            tx_busy=(bits&4u)!=0;modbus_busy=(bits&8u)!=0;
            ota_is_working=(bits&16u)!=0;valid=(bits&32u)!=0;
            due=(bits&64u)!=0;device_in_connection_state=(bits&128u)!=0;
            now=100u+((bits&256u)?12801u:0u);mask_calls=0;
            app_power_process(&due);
            int blocked=SH3673510_FIXED_UART_BLOCKS_PM || !switch_high || !flash_locked ||
                tx_busy || modbus_busy || ota_is_working || !valid || due || (bits&256u);
            assert(mask==(blocked?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
            assert(observed_allowed==!blocked && s_low_power_mode==!blocked && mask_calls==1);
        }
        return 0;
    }
    '''
    for fixed_gate in (1, 0):
        run_c(defines + '\n#define SH3673510_FIXED_UART_BLOCKS_PM %d\n' % fixed_gate +
              fixture.replace('/* PRODUCTION */', body),
              flags=('-Wno-unused-function',), name='sh-suspend-current')

if __name__ == "__main__":
    check_sh3673510_sample_schedule_host_check()
    check_sh3673510_sleep_host_check()
    check_sh_protective_sleep()
    check_sh_suspend_current_gates()
