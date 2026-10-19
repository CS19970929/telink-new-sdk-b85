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
    source = selected_source(APP / 'app.c', defines=('-DBMS_SERVICE_DIAG_ENABLE=1',))


    def function(name):
        return extract_function(source, 'static void ' + name + '(')

    assert 'bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup)' in source
    assert '    app_sample_task();' in source
    assert function('app_sample_task').count('bms_afe_sample();') == 1
    assert 'test_task_tick' not in source and 'hello World!!!' not in source
    period = (APP/'bms_timing.h').read_text() + '\n' + re.search(
        r'(?m)^#define APP_SAMPLE_PERIOD_US[^\n]*', (APP/'app.h').read_text()).group()
    code = r'''
    #define BMS_SERVICE_DIAG_ENABLE 1
    #include <stdint.h>
    #include <stdio.h>
typedef enum { BMS_AFE_SAMPLE_OK, BMS_AFE_SAMPLE_NOT_READY, BMS_AFE_SAMPLE_ERROR } bms_afe_sample_status_t;
    typedef uint8_t u8;
    typedef uint32_t u32;
    #define SYSTEM_TIMER_TICK_1US 16u
    #define MODE_FACTORY 1
    typedef struct { int32_t raw_current_ma; int32_t current_ma; u32 sample_tick_32k; } bms_afe_aux_measurements_t;
    static u32 s_sample_tick, fake_tick, scheduled_tick, sample_cost;
    static volatile u8 s_sample_due;
    static unsigned samples, soc_calls, diag_calls, failures;
    static u8 valid, last_valid;
    static int32_t last_current;
    static u32 clock_time(void) { return fake_tick; }
    static u32 pm_get_32k_tick(void) { return 4321; }
    static int clock_time_exceed(u32 since, u32 us) { return (u32)(fake_tick-since) > us*16u; }
    static void bls_pm_setAppWakeupLowPower(u32 tick, unsigned enable) {
        if (!enable) ++failures;
        scheduled_tick = tick;
    }
    static bms_afe_sample_status_t bms_afe_sample(void) { bms_afe_sample_status_t r=BMS_AFE_SAMPLE_OK; ++samples; fake_tick += sample_cost; return r; }
    static void bms_features_service(void){}
    static void bms_afe_apply_outputs(void){}
    static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *s) {
        if (valid) { s->raw_current_ma = 1230; s->current_ma = 1234; s->sample_tick_32k = 123; }
        return valid;
    }
    static void app_update_soc_from_sample(u8 v, int32_t c) {
        ++soc_calls; last_valid=v; last_current=c;
    }
    static void app_diag_poll_runtime(u8 v, int32_t raw, int32_t c, u32 t) {
        (void)v; (void)c; (void)t; (void)raw; ++diag_calls;
    }
    /* PRODUCTION */
    #define CHECK(c) do { if (!(c)) { ++failures; printf("FAIL %d: %s\n", __LINE__, #c); } } while(0)
    int main(void) {
        valid=1; s_sample_tick=100; fake_tick=100;
        app_sample_task(); CHECK(samples==0);
        fake_tick += APP_SAMPLE_PERIOD_US*16u + 1;
        app_sample_task(); CHECK(samples==1 && soc_calls==1 && diag_calls==1);
        CHECK(last_valid && last_current==1234);
        CHECK(scheduled_tick==s_sample_tick+APP_SAMPLE_PERIOD_US*16u);
        app_sample_task(); CHECK(samples==1);
        app_sample_wakeup(0); app_sample_wakeup(0); CHECK(samples==1);
        app_sample_task(); CHECK(samples==2); /* multiple IRQ marks coalesce */
        valid=0; app_sample_wakeup(0); app_sample_task();
        CHECK(!last_valid && last_current==0);
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
        match = re.search(r'(?m)^(?:static )?(?:void|int|uint8_t|uint32_t|u32) ' + name + r'\(', source)
        assert match, name
        return extract_function(source, match.group(0))

    guard = (APP / 'bms_afe_guard.c').read_text(encoding='utf-8')
    state = '#include "bms_afe_registers.h"\n' + guard[guard.index('typedef struct'):guard.index('uint8_t bms_afe_bus_access_allowed')]
    policy = re.search(r'typedef struct\s*\{[^}]*\} bms_afe_driver_policy_t;', (ROOT/'bms/core/bms_afe_driver.h').read_text(encoding='utf-8'), re.S).group()
    policy += '\n' + re.search(r'const bms_afe_driver_policy_t bms_afe_driver_policy = [^;]+;', (APP/'sh3673510_bms.c').read_text(encoding='utf-8')).group()
    body = '#include "bms_debug_log.h"\n' + policy + '\n' + state + '\n'.join([
        function('sh3673510_control.c', 'sh3673510_control_sleep'),
        function('sh3673510_control.c', 'sh3673510_control_wake'),
        function('sh3673510_bms.c', 'bms_afe_driver_sleep'),
        function('bms_afe_guard.c', 'bms_afe_bus_access_allowed'),
        function('bms_afe_guard.c', 'inhibit_local'),
        function('bms_afe_guard.c', 'best_effort_shutdown'),
        function('bms_afe_guard.c', 'enter_failsafe_wait'),
        function('bms_afe_guard.c', 'note_invalid'),
        function('bms_afe_guard.c', 'bms_afe_sleep'),
        function('bms_sleep_config.c', 'sleep_elapsed_limit'),
    ])
    fixture = '#define BMS_AFE_BACKEND 2\n#define BMS_AFE_BACKEND_DVC1124 1\n' + (ROOT / 'tests/fixtures/sh3673510_sleep.c').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='sh3510-sleep-') as tmp:
        c, exe = Path(tmp) / 'check.c', Path(tmp) / 'check.exe'
        c.write_text(fixture.replace('/* PRODUCTION */', body), encoding='utf-8')
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                        '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                        *host_includes(ROOT), str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('SH production sleep/control/backend/guard fault injection: PASS')

def check_sh_protective_sleep():
    """执行当前 SH 产品的保护计时/深睡保持生产函数，边界均注入失败。"""
    from pathlib import Path
    from project_paths import selected_source
    from validation_support import function, run_c
    root = Path(__file__).resolve().parents[1]
    source = selected_source(root / 'bms/app/app_power.c')
    startup = (root / 'bms/platform/telink/main.c').read_text(encoding='utf-8')
    unused_start = startup.index('static const GPIO_PinTypeDef unused_afe_pins[]')
    unused_config = startup[unused_start:startup.index('#endif', unused_start)]
    assert startup.index('gpio_init(!deepRetWakeUp)') < unused_start < startup.index('while (app_power_boot_sleep_hold())')
    assert startup.index('clock_init(SYS_CLK_TYPE)') < startup.index('while (app_power_boot_sleep_hold())')
    assert startup.index('while (app_power_boot_sleep_hold())') < startup.index('wd_start()') < startup.index('user_init_normal()')
    run_c(r'''
    #include <stdint.h>
    #include <assert.h>
    typedef uint8_t u8;
    typedef int GPIO_PinTypeDef;
    #define BMS_BOARD_AFE_ALARM_PIN 0
    #define BMS_BOARD_AFE_RESET_OUT_PIN 1
    #define AS_GPIO 1
    #define PM_PIN_UP_DOWN_FLOAT 0
    #define Level_Low 0
    static int input[2]={1,1}, output[2]={1,1}, pull[2]={1,1}, wake[2]={1,1}, calls[2];
    static void gpio_set_output_en(int pin,int en) { assert(!en); output[pin]=en; ++calls[pin]; }
    static void gpio_set_input_en(int pin,int en) { assert(!en); input[pin]=en; ++calls[pin]; }
    static void gpio_set_func(int pin,int func) { assert(!output[pin]&&func==AS_GPIO); ++calls[pin]; }
    static void gpio_setup_up_down_resistor(int pin,int value) { pull[pin]=value; ++calls[pin]; }
    static void cpu_set_gpio_wakeup(int pin,int level,int en) { (void)level; wake[pin]=en; ++calls[pin]; }
    int main(void) {
    /* STARTUP */
        for(int pin=0;pin<2;++pin) assert(!input[pin]&&!output[pin]&&!pull[pin]&&!wake[pin]&&calls[pin]==5);
        return 0;
    }
    '''.replace('/* STARTUP */', unused_config), name='sh-unused-afe-pins')
    start = source.index('#define APP_COMMAND_REPLY_MS')
    end = source.index('static app_protective_sleep_t s_protective_sleep;', start)
    body = source[start:end] + 'static app_protective_sleep_t s_protective_sleep;\n'
    policy = (root / 'bms/core/bms_sleep_config.c').read_text()
    body += policy[policy.index('static uint32_t sleep_elapsed_limit('):]
    body += '\n'.join(function(source, signature) for signature in (
        'static u32 app_pm_take_elapsed_seconds(',
        'u8 app_power_boot_sleep_hold(',
        'static void app_protective_wakeup_pin(', 'static void app_protective_sleep_hold(',
        'static void app_prepare_deep_sleep(',
        'static void app_enter_protective_sleep(', 'static u8 app_command_sleep_poll(',
        'static u8 app_protective_sleep_poll(',
        'uint8_t app_power_prepare_loop('))
    # 保留策略宏分支，分别覆盖开启/关闭；其他函数使用实际SH预处理分支。
    raw = (root / 'bms/app/app_power.c').read_text(encoding='utf-8')
    body += function(raw, 'static u8 app_sh_explicit_sleep_poll(')
    import re
    body = '\n'.join(re.findall(r'^#define APP_SH_SWITCH_WAKE_(?:TAG|MASK)[^\n]*', raw, re.M)) + '\n' + body
    body += '\n' + function(source, 'int app_power_before_suspend(')
    body += '\n' + function(source, 'static uint8_t app_current_prevents_suspend(')
    body += '\n' + function(source, 'void app_power_process(')
    fixture = (root / 'tests/fixtures/protective_sleep.c').read_text(encoding='utf-8')
    for enabled in (1, 0):
        run_c('#define BMS_PRODUCT_SWITCH_SLEEP_ENABLE %du\n' % enabled +
              fixture.replace('/* PRODUCTION */', body), flags=('-DBMS_SERVICE_DIAG_ENABLE=1',), name='sh-protective-sleep')

def check_sh_suspend_current_gates():
    """运行真实 SH 普通 suspend 入口；串口静默资格与其他安全条件独立。"""
    from pathlib import Path
    import re
    from project_paths import selected_source
    from validation_support import function, run_c
    root = Path(__file__).resolve().parents[1]
    source = selected_source(root / 'bms/app/app_power.c', defines=('-DBMS_SERVICE_DIAG_ENABLE=1',))
    defines = re.search(r'^#define APP_SUSPEND_EXIT_CURRENT_MA[^\n]*', source, re.M).group()
    defines += '\n' + re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                               (root / 'bms/core/bms_soc.h').read_text(), re.M).group()
    body = '\n'.join(function(source, signature) for signature in (
        'static uint8_t app_current_prevents_suspend(',
        'void app_power_process('))
    fixture = r'''
    #include <stdint.h>
    #include <stdbool.h>
    #include <assert.h>
    #include <string.h>
    static uint8_t register_debug_hold;
    #define bms_afe_register_debug_held() register_debug_hold
    typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
    typedef struct { int unused; } app_pm_elapsed_ctx_t;
    typedef struct { int32_t current_ma; u32 sample_time_ms; } bms_afe_aux_measurements_t;
    #define BMS_SOC_MAX_SAMPLE_GAP_MS 400u
    #define BMS_SAMPLE_MAX_POLL_GAP_MS 400u
    #define BMS_BOARD_SWITCH_PIN 0
    #define BMS_BOARD_INT_WK_MCU_PIN 1
    #define Level_Low 0
    #define SUSPEND_DISABLE 0
    #define SUSPEND_ADV 1
    #define SUSPEND_CONN 2
    static bool s_low_power_mode, deepsleep_en;
    static u32 s_command_ms;
    typedef struct { u8 reason; u32 elapsed_ms, delay_ms; } app_sleep_status_t;
    #define BMS_SLEEP_REASON_NONE 0
    #define BMS_SLEEP_REASON_COMMAND 1
    #define BMS_SLEEP_REASON_SWITCH 3
    static int valid=1, flash_locked=1, tx_busy, modbus_busy, ota_is_working;
    static int idle_ready=1;
    static int mask, observed_allowed, mask_calls;
    static u32 now=100;
    static bms_afe_aux_measurements_t measurement={0,100};
    static u32 bms_time_ms(void){return now;}
    static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m){*m=measurement;return valid;}
    static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t *c){(void)c;return 0;}
    static u8 app_protective_sleep_poll(u32 seconds,
          const bms_afe_aux_measurements_t *m, u8 valid, app_sleep_status_t *s){
        (void)seconds;(void)m;(void)valid;memset(s,0,sizeof(*s));return 0;
    }
    static u8 app_sh_explicit_sleep_poll(void){return 0;}
    static int modbus_uart_suspend_ready(void){return idle_ready&&!tx_busy&&!modbus_busy;}
    static void bms_sleep_status_publish(u8 r,u32 b,u32 e,u32 d,u32 retry,u8 allowed){
        (void)r;(void)b;(void)e;(void)d;(void)retry;observed_allowed=allowed;
    }
    static int app_flash_lock_restore_enabled(void){return flash_locked;}
    static void bls_pm_setSuspendMask(int value){mask=value;++mask_calls;}
    static void bls_pm_setManualLatency(int value){(void)value;}
    /* PRODUCTION */
    int main(void){
        volatile u8 due=0;
        int32_t currents[]={INT32_MIN,-500,-201,-200,-199,0,199,200,201,500,INT32_MAX};
        for(unsigned i=0;i<sizeof(currents)/sizeof(currents[0]);i++){
            measurement.current_ma=currents[i];app_power_process(&due);
            int blocked=currents[i]<=-200 || currents[i]>=200;
            assert(mask==(blocked?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
            assert(observed_allowed==!blocked);
        }
        measurement.current_ma=0;
        valid=0;app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        valid=1;now=measurement.sample_time_ms+401u;
        app_power_process(&due);assert(mask==(SUSPEND_ADV|SUSPEND_CONN) && observed_allowed);
        now=100;due=1;app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        due=0;flash_locked=0;app_power_process(&due);assert(mask==SUSPEND_DISABLE && !observed_allowed);
        /* 各门禁组合：控制输出与最终诊断必须一致，每轮只设置一次 mask。 */
        for(unsigned bits=0;bits<128u;bits++){
            idle_ready=(bits&1u)!=0;flash_locked=(bits&2u)!=0;
            tx_busy=(bits&4u)!=0;modbus_busy=(bits&8u)!=0;
            ota_is_working=(bits&16u)!=0;valid=(bits&32u)!=0;
            due=(bits&64u)!=0;
            now=100u;mask_calls=0;
            app_power_process(&due);
            int blocked=!idle_ready || !flash_locked ||
                tx_busy || modbus_busy || ota_is_working || !valid || due;
            assert(mask==(blocked?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
            assert(observed_allowed==!blocked && s_low_power_mode==!blocked && mask_calls==1);
        }
        register_debug_hold=1;s_low_power_mode=true;mask=-1;mask_calls=0;
        app_power_process(&due);
        assert(mask==SUSPEND_DISABLE && !s_low_power_mode && mask_calls==1);
        return 0;
    }
    '''
    run_c(defines + '\n' + fixture.replace('/* PRODUCTION */', body),
          flags=('-DBMS_SERVICE_DIAG_ENABLE=1','-Wno-unused-function',), name='sh-suspend-current')

if __name__ == "__main__":
    check_sh3673510_sample_schedule_host_check()
    check_sh3673510_sleep_host_check()
    check_sh_protective_sleep()
    check_sh_suspend_current_gates()
