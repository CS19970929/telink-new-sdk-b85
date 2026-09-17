"""Execute the actual application wake scheduling policy with SDK calls stubbed."""
import os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MOD=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
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
