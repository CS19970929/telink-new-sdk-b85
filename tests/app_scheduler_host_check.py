"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_app_recovery_wakeup_host_check():
    print("CHECK app_recovery_wakeup_host_check", flush=True)
    """Execute the actual application wake scheduling policy with SDK calls stubbed."""
    import os,subprocess,tempfile
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    ROOT=Path(__file__).resolve().parents[1]
    MOD = Sources(ROOT)
    s=(MOD/'app.c').read_text(encoding='utf-8')
    a=s.index('static void app_schedule_sample_wakeup(');b=s.index('\n}',a)+2
    policy=s[a:b]
    assert 'app_sample_task();' in s[s.index('void main_loop'):]
    assert 'app_schedule_sample_wakeup();' in s[s.index('static void app_sample_task'):]
    assert 'bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup);' in s
    code="""
    #include <stdint.h>
    #include <assert.h>
    #define APP_SAMPLE_PERIOD_US 200000u
    #define SYSTEM_TIMER_TICK_1US 16u
    static uint32_t s_sample_tick, deadline;
    static uint8_t enabled;
    static void bls_pm_setAppWakeupLowPower(uint32_t tick,uint8_t en){deadline=tick;enabled=en;}
    """+policy+"""
    int main(void){
     s_sample_tick=100;app_schedule_sample_wakeup();assert(enabled && deadline==3200100u);
     s_sample_tick=0xfffffff0u;app_schedule_sample_wakeup();assert(deadline==(uint32_t)(0xfffffff0u+3200000u));
     s_sample_tick=3200100u;app_schedule_sample_wakeup();assert(enabled && deadline==6400100u);
     return 0;
    }
    """
    with tempfile.TemporaryDirectory(prefix='d008-recovery-wake-') as folder:
     p=Path(folder)/'test.c';p.write_text(code)
     exe=Path(folder)/'test.exe'
     subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
     subprocess.run([str(exe)],check=True)
    print('PASS mandatory 200ms wakeup: next deadline and tick wrap')

def check_app_scheduler_host_check():
    print("CHECK app_scheduler_host_check", flush=True)
    """Run production scheduling code against observable hardware stubs."""
    from validation_support import function
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import os
    import re
    import shlex
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    MOD = Sources(ROOT)



    def main():
        app = selected_source(MOD / "app.c", defines=("-DBMS_SERVICE_DIAG_ENABLE=1",))
        power = selected_source(MOD / "app_power.c", defines=("-DBMS_SERVICE_DIAG_ENABLE=1",))
        # Exercise the actual event deadline gate, stub only event payload collection.
        event = function(app, "static void app_storage_1s_task(")
        event = event[event.index("    _attribute_data_retention_"):event.index("\tmemset(&sample")]
        checkpoint = re.search(r"bms_state_store_poll\([^;]+;",function(app,"static void app_storage_1s_task(")).group(0)
        scheduler = "\n".join([
            function(power, "uint8_t app_power_prepare_loop("),
            function(app, "static void app_sample_task("),
            "static void app_storage_1s_task(void){\n" + event + "note('E');" + checkpoint + "}",
            function(app, "_attribute_no_inline_ void main_loop("),
        ])
        with tempfile.TemporaryDirectory(prefix="d008-scheduler-") as folder:
            for name, production in (("scheduler", scheduler),):
                fixture = (ROOT / "tests/fixtures/d008_scheduler" / (name + ".c")).read_text()
                path = Path(folder) / (name + ".c")
                path.write_text(fixture.replace("/* PRODUCTION_SOURCE */", production))
                exe = Path(folder) / (name + ".exe")
                subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
                    "-std=c99", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True)
        print("PASS scheduler order, deadlines, wrap, overrun, callback, invalid sample, shutdown; aging/reentry")


    def check_time_ms_host_check():
    from validation_support import read, run_c
    import re
    source = re.sub(r'^#include[^\n]*', '', read('bms/platform/telink/bms_time.c'), flags=re.M)
    code = r'''
#include <stdint.h>
#include <assert.h>
static uint32_t raw;
static uint32_t pm_get_32k_tick(void){return raw;}
''' + source + r'''
int main(void){
 raw=UINT32_MAX-15u;bms_time_init();assert(bms_time_ms()==0u);
 raw+=31u;assert(bms_time_ms()==0u);raw++;assert(bms_time_ms()==1u);
 for(unsigned i=0;i<32u;i++){raw++;(void)bms_time_ms();}assert(bms_time_ms()==2u);
 /* 普通Suspend后保留时基；近完整原始周期不溢出分数累加。 */
 uint32_t before=bms_time_ms();raw+=320000u;assert(bms_time_ms()-before==10000u);
 uint64_t expected=(uint64_t)s_fraction_ticks+UINT32_MAX;
 before=bms_time_ms();raw+=UINT32_MAX;
 assert(bms_time_ms()-before==(uint32_t)(expected/32u));
 s_elapsed_ms=UINT32_MAX;raw+=32u;assert(bms_time_ms()==0u);
 bms_time_init();assert(bms_time_ms()==0u);
 /* 毫秒边界前启动：elapsed==200不证明满200ms，必须继续等待。 */
 raw+=31u;uint32_t start=bms_time_ms();raw+=6369u;
 assert(bms_time_ms()-start==200u);raw+=32u;assert(bms_time_ms()-start>200u);
 return 0;
}
'''
    run_c(code, name='time-ms')


if __name__ == "__main__":
        main()

if __name__ == "__main__":
    check_app_recovery_wakeup_host_check()
    check_app_scheduler_host_check()
    check_time_ms_host_check()
