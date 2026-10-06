"""Execute the diagnostic core and actual Modbus ingress with side-effect spies."""
from validation_support import function
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import os, re, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
MOD = Sources(ROOT)

def main():
    source=(MOD/'modbus_rtu.c').read_text(encoding='utf-8')
    ingress=function(source,'int modbus_on_frame(')
    fixture=(ROOT/'tests/fixtures/bms_diag.c').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='bms-diag-') as directory:
        p=Path(directory)/'test.c';p.write_text(fixture.replace('/* MODBUS */',ingress),encoding='utf-8')
        exe=Path(directory)/'test.exe'
        for defines in ([], ['-DBMS_DEBUG_LOG_ENABLE=1', '-DBMS_DEBUG_LOG_LEVEL=4'],
                        ['-DBMS_DIAG_TRACE_ENABLE=0'],
                        ['-DBMS_DIAG_TRACE_ENABLE=0', '-DBMS_DEBUG_LOG_ENABLE=1', '-DBMS_DEBUG_LOG_LEVEL=4']):
            subprocess.run(shlex.split(os.environ.get('CC','cc'))+defines+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*host_includes(ROOT),str(p),str(MOD/'bms_diag.c'),str(MOD/'bms_debug_log.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
