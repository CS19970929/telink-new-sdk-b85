"""Run D008 COV/CUV recovery across invalid samples and scheduler gaps."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
source = (APP / 'dvc1124_bms.c').read_text(encoding='utf-8')
begin = source.index('static uint8_t dvc_recovery_stable(')
end = source.index('static void dvc_merge_hw_faults(', begin)
production = '#define DVC_BMS_SAMPLE_PERIOD_MS 200u\n' + source[begin:end]
fixture = r'''
#include <stdint.h>
#include <assert.h>
#include "dvc1124.h"
typedef struct { uint16_t cov_recover_mv, cuv_recover_mv, cov_recover_ms, cuv_recover_ms; } bms_afe_hw_profile_t;
static struct { uint16_t u16VCellMax, u16VCellMin; } g_stCellInfoReport = {3400, 3200};
static unsigned writes;
static uint8_t profile_ok=1, clear_ok=1, read_ok=1;
static uint8_t bms_afe_hw_profile_get(bms_afe_hw_profile_t *p) {
    p->cov_recover_mv=3500; p->cuv_recover_mv=3100;
    p->cov_recover_ms=p->cuv_recover_ms=2000; return profile_ok;
}
uint8_t DVC1124_ClearAlarmFlags(uint8_t mask) { (void)mask; ++writes; return clear_ok; }
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *v,uint8_t n) { (void)reg; (void)n; *v=0; return read_ok; }
/* PRODUCTION */
int main(void) {
    uint8_t alarm=DVC1124_ALARM_COV_MASK|DVC1124_ALARM_CUV_MASK;
    uint32_t tick=0xffff0000u;
    unsigned i,gap,before;
    for(gap=1;gap<10;++gap) {
        dvc_clear_recovered_hw_latches(0,0,0);
        before=writes;
        for(i=0;i<gap;++i) { tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm); }
        dvc_clear_recovered_hw_latches(0,0,0);
        for(i=0;i<9;++i) { tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm); }
        assert(writes==before);
        tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==0);
    }
    dvc_clear_recovered_hw_latches(0,0,0); before=writes;
    for(i=0;i<9;++i) { tick+=6400; dvc_clear_recovered_hw_latches(alarm,1,tick); }
    tick+=32000; /* sleep/reinit gap even without an explicitly invalid sample */
    assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm && writes==before);
    for(i=0;i<8;++i) { tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm); }
    tick+=6400; clear_ok=0; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm);
    clear_ok=1; read_ok=0; tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm);
    read_ok=1; profile_ok=0; tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm);
    profile_ok=1; before=writes;
    for(i=0;i<9;++i) { tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==alarm); }
    assert(writes==before);
    tick+=6400; assert(dvc_clear_recovered_hw_latches(alarm,1,tick)==0);
    return 0;
}
'''
# The production invalid-acquisition branch must reach the qualification reset.
invalid = source[source.index('if (!snapshot.valid) {'):source.index('memset(&sw, 0, sizeof(sw));')]
assert 'dvc_clear_recovered_hw_latches(0u, 0u, 0u);' in invalid
with tempfile.TemporaryDirectory(prefix='d008-voltage-recovery-') as tmp:
    c = Path(tmp) / 'check.c'
    exe = Path(tmp) / 'check.exe'
    c.write_text(fixture.replace('/* PRODUCTION */', production), encoding='utf-8')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-I', str(APP), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('D008 COV/CUV valid-sample continuity, wrap and fault injection: PASS')
