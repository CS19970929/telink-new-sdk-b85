"""实际诊断函数体与PM/寄存器替身；不证明真实Suspend长期可靠。"""
from validation_support import read, without_includes, run_c

source = without_includes(read('bms/platform/telink/bms_stall_diag.c'))
fixture = read('tests/fixtures/stall_diag.c').replace('/* PRODUCTION */', source)
run_c(fixture, flags=['-DBMS_STALL_DIAG_ENABLE=1'], name='stall-diag')
run_c(r'''
#include <assert.h>
#include "bms_stall_diag.h"
int main(void) {
    unsigned side_effect=0u;uint8_t bytes[250];
    bms_stall_diag_stage(++side_effect,++side_effect);
    bms_stall_diag_time(++side_effect);
    bms_stall_diag_time_begin();
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
