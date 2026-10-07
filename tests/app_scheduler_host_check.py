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
    static uint8_t pending,enabled;
    static uint8_t bms_afe_current_recovery_pending(void){return pending;}
    static void bls_pm_setAppWakeupLowPower(uint32_t tick,uint8_t en){deadline=tick;enabled=en;}
    """+policy+"""
    int main(void){
     s_sample_tick=100;pending=0;app_schedule_sample_wakeup();assert(enabled==BMS_APP_SAMPLE_WAKEUP_ENABLE);
     pending=1;app_schedule_sample_wakeup();assert(enabled && deadline==3200100u);
     s_sample_tick=0xfffffff0u;app_schedule_sample_wakeup();assert(deadline==(uint32_t)(0xfffffff0u+3200000u));
     pending=0;app_schedule_sample_wakeup();assert(enabled==BMS_APP_SAMPLE_WAKEUP_ENABLE);
     if(!BMS_APP_SAMPLE_WAKEUP_ENABLE)assert(deadline==0);
     return 0;
    }
    """
    with tempfile.TemporaryDirectory(prefix='d008-recovery-wake-') as folder:
     p=Path(folder)/'test.c';p.write_text(code)
     for enable in (0,1):
      exe=Path(folder)/f'test{enable}.exe'
      subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror',f'-DBMS_APP_SAMPLE_WAKEUP_ENABLE={enable}',str(p),'-o',str(exe)],check=True)
      subprocess.run([str(exe)],check=True)
    print('PASS wakeup OFF/ON: idle, fault recovery, release cancellation, tick wrap')

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
        app = selected_source(MOD / "app.c")
        # Exercise the actual event deadline gate, stub only event payload collection.
        event = function(app, "static void app_event_log_1s_task(")
        event = event[event.index("    _attribute_data_retention_"):event.index("\tmemset(&sample")]
        scheduler = "\n".join([
            function(app, "static void app_sample_task("),
            "static void app_event_log_1s_task(void){\n" + event + "note('E');}",
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


    if __name__ == "__main__":
        main()

if __name__ == "__main__":
    check_app_recovery_wakeup_host_check()
    check_app_scheduler_host_check()
