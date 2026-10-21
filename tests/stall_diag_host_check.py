"""实际诊断函数体与PM/寄存器替身；不证明真实Suspend长期可靠。"""
from validation_support import read, without_includes, run_c

source = without_includes(read('bms/platform/telink/bms_stall_diag.c'))
fixture = read('tests/fixtures/stall_diag.c').replace('/* PRODUCTION */', source)
fixture = fixture.replace('/* SLEEP STATUS */', without_includes(read('bms/core/bms_sleep_status.c')))
run_c(fixture, flags=['-DBMS_STALL_DIAG_ENABLE=1'], name='stall-diag')
run_c(r'''
#include <assert.h>
#include "bms_stall_diag.h"
int main(void) {
    unsigned side_effect=0u;uint8_t bytes[250];
    bms_stall_diag_stage(++side_effect,++side_effect);
    bms_stall_diag_time(++side_effect);
    bms_stall_diag_time_begin();
    bms_stall_diag_sleep_reason(++side_effect);
    bms_stall_diag_deep_commit();
    bms_stall_diag_bus(++side_effect,++side_effect);
    bms_stall_diag_afe(++side_effect,++side_effect,++side_effect,++side_effect);
    bms_stall_diag_guard(++side_effect,++side_effect,++side_effect);
    assert(!side_effect);
    assert(bms_stall_diag_read(0x3A00u,4u,bytes));
    assert(bytes[0]==0x53u && bytes[1]==0x54u && !bytes[4] && !bytes[5]);
    assert(!bms_stall_diag_read(0x3A9Fu,2u,bytes));
    assert(!bms_stall_diag_read(0x3A00u,126u,bytes));
    assert(!bms_stall_diag_read(0x3A00u,1u,0));
    return 0;
}
''', name='stall-diag-disabled')

# 离线判读必须将“重新进入main”和“异常复位”分开，兼容旧schema且保留不确定性。
decoder = {'__name__':'stall_decoder'}
exec(compile(read('bms_tools/decode_stall_diag.py'),'decode_stall_diag.py','exec'),decoder)
decode, compare_boot = decoder['decode'], decoder['compare_boot']
baseline = [0]*160
baseline[:4] = [0x5354,2,1,160]
baseline[6:9] = [0x1234,0x5678,1]
current = baseline.copy()
current[8] = 0x0102
current[13] = 0xD7
decoded = decode(current)
assert decoded['previous_deep_call_marker_valid'] and decoded['previous_deep_call_reason']=='AFE'
notes = compare_boot(decoded,decode(baseline))
assert '重新进入main' in notes[0] and 'DeepSleep' in notes[1]
current[13] = 0
notes = compare_boot(decode(current),decode(baseline))
assert '没有有效DeepSleep' in notes[1]
current[1] = 1
current[13] = 0xD7
assert not decode(current)['previous_deep_call_marker_valid']
current[1] = 2
current[9] = 1
notes = compare_boot(decode(current),decode(baseline))
assert any('由0变1' in note for note in notes)
baseline[9] = 1
notes = compare_boot(decode(current),decode(baseline))
assert any('粘滞旧值' in note for note in notes)
current = baseline.copy()
current[4] = 3
notes = compare_boot(decode(current),decode(baseline))
assert '普通Suspend' in notes[1]
# 冻结副本：进入/返回次数配对才解释SDK的完整返回位，避免把上次返回当成本次。
current[80+10*2+1] = 1
current[80+11*2+1] = 1
current[80+17*2:80+17*2+2] = [0x4000,8]
decoded = decode(current)
assert decoded['first_gap']['pm_return_flags']['sdk_enter_suspend_bit']
assert decoded['first_gap']['pm_return_flags']['pad']
current[80+11*2+1] = 0
assert decode(current)['first_gap']['pm_return_flags'] is None
