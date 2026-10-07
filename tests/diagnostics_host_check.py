"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_bms_diag_host_check():
    print("CHECK bms_diag_host_check", flush=True)
    """Execute the diagnostic core and actual Modbus ingress with side-effect spies."""
    from validation_support import ROOT, read, function, run_c

    def main():
        source=read('bms/core/modbus_rtu.c')
        ingress=function(source,'int modbus_on_frame(')
        fixture=read('tests/fixtures/bms_diag.c').replace('/* MODBUS */',ingress)
        for defines in ([], ['-DBMS_DEBUG_LOG_ENABLE=1', '-DBMS_DEBUG_LOG_LEVEL=4'],
                        ['-DBMS_DIAG_TRACE_ENABLE=0'],
                        ['-DBMS_DIAG_TRACE_ENABLE=0', '-DBMS_DEBUG_LOG_ENABLE=1', '-DBMS_DEBUG_LOG_LEVEL=4']):
            run_c(fixture, ['bms/core/bms_diag.c','bms/core/bms_debug_log.c'],
                  ['-Wno-unused-parameter','-I',str(ROOT/'tests/fixtures/d014_safety_loop/include'),*defines],
                  name='bms-diag')
    if __name__=='__main__':main()

def check_runtime_debug_log_host_check():
    print("CHECK runtime_debug_log_host_check", flush=True)
    """Run the real RAM logger: disabled side effects, filtering, bounds and wrap."""
    from pathlib import Path
    import os
    import shlex
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    CORE = ROOT / "bms/core"
    CODE = r'''
    #include <stdint.h>
    #include <assert.h>
    static uint32_t tick;
    uint32_t bms_diag_tick(void) { return tick; }
    /* Include the implementation to seed a u32 wrap without billions of writes. */
    #include "bms_debug_log.c"
    static uint16_t word(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
    int main(void) {
        uint8_t bytes[250]; int side_effect=0;
        uint8_t request[8]={1,3,0x30,0,0,16,0,0};
        tick=1234; bms_debug_log_init();
        assert(bms_debug_log_read(BMS_DEBUG_LOG_BASE,16,bytes));
        assert(word(bytes)==0x4c47 && word(bytes+2)==1);
        assert(word(bytes+4)==BMS_DEBUG_LOG_ENABLE);
        assert(!bms_debug_log_read(BMS_DEBUG_LOG_BASE,0,bytes));
        assert(!bms_debug_log_read(BMS_DEBUG_LOG_BASE,126,bytes));
        assert(!bms_debug_log_read(BMS_DEBUG_LOG_BASE,1,0));
        assert(!bms_debug_log_read(BMS_DEBUG_LOG_BASE-1,2,bytes));
        assert(!bms_debug_log_read(BMS_DEBUG_LOG_END-1,2,bytes));
        assert(!bms_debug_log_overlaps(0xffff,1));
        assert(bms_debug_log_overlaps(BMS_DEBUG_LOG_BASE-1,2));
        assert(bms_debug_log_is_read(request,8));
        assert(!bms_debug_log_is_read(request,7));
        assert(!bms_debug_log_is_read(0,8));
    #if !BMS_DEBUG_LOG_ENABLE
        BMS_LOG(BMS_LOG_ERROR,BMS_LOG_SYSTEM,1,++side_effect,++side_effect);
        assert(side_effect==0);
        assert(bms_debug_log_read(BMS_DEBUG_LOG_BASE+16,125,bytes));
        for(unsigned i=0;i<250;i++) assert(bytes[i]==0);
    #elif BMS_DEBUG_LOG_LEVEL == 2
        BMS_LOG(BMS_LOG_DEBUG,BMS_LOG_PROTECT,1,++side_effect,0);
        BMS_LOG(BMS_LOG_ERROR,BMS_LOG_SYSTEM,1,++side_effect,0);
        assert(side_effect==0 && s_count==0);
        BMS_LOG(BMS_LOG_WARN,BMS_LOG_PROTECT,1,++side_effect,0);
        assert(side_effect==1 && s_count==1);
    #else
        (void)side_effect;
        for(uint32_t i=0;i<70;i++) {
            tick=UINT32_MAX-20u+i;
            BMS_LOG(BMS_LOG_DEBUG,BMS_LOG_SOC,33,i,~i);
        }
        assert(s_count==64 && s_next_sequence==70 && s_overwritten==6);
        assert(bms_debug_log_read(BMS_DEBUG_LOG_BASE+16,10,bytes));
        assert(word(bytes)==0 && word(bytes+2)==64);
        assert(word(bytes+8)==0x0403 && word(bytes+10)==33);
        assert(word(bytes+12)==0 && word(bytes+14)==64);
        assert(word(bytes+16)==0xffff && word(bytes+18)==(uint16_t)~64u);
        for(int i=0;i<100;i++) assert(bms_debug_log_read(BMS_DEBUG_LOG_BASE,16,bytes));
        assert(s_next_sequence==70); /* Reads cannot create/consume log entries. */
        s_next_sequence=UINT32_MAX; s_overwritten=UINT32_MAX;
        BMS_LOG(BMS_LOG_ERROR,BMS_LOG_SYSTEM,1,0,0);
        assert(s_next_sequence==0 && s_records[63][0]==0xffff && s_records[63][1]==0xffff);
        BMS_LOG(BMS_LOG_ERROR,BMS_LOG_SYSTEM,1,0,0);
        assert(s_next_sequence==1 && s_records[0][0]==0 && s_records[0][1]==0);
        assert(s_overwritten==UINT32_MAX);
        bms_debug_log_write(5,0,1,0,0); bms_debug_log_write(1,32,1,0,0);
        assert(s_next_sequence==1);
        bms_debug_log_init(); assert(s_count==0 && s_next_sequence==0 && s_overwritten==0);
    #endif
        return 0;
    }
    '''

    with tempfile.TemporaryDirectory(prefix="bms-runtime-log-") as directory:
        source = Path(directory) / "test.c"
        source.write_text(CODE, encoding="utf8")
        for defines in ([], ["-DBMS_DEBUG_LOG_ENABLE=1", "-DBMS_DEBUG_LOG_LEVEL=4"],
                        ["-DBMS_DEBUG_LOG_ENABLE=1", "-DBMS_DEBUG_LOG_LEVEL=2", "-DBMS_DEBUG_LOG_MODULE_MASK=4"]):
            exe = Path(directory) / "test.exe"
            command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                       "-I", str(CORE), *defines, str(source), "-o", str(exe)]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)
    print("PASS real runtime logger: no-evaluation when disabled/filtered, bounds, wire endian, overwrite, u32 wrap, reset")

if __name__ == "__main__":
    check_bms_diag_host_check()
    check_runtime_debug_log_host_check()
