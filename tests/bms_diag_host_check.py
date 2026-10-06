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
