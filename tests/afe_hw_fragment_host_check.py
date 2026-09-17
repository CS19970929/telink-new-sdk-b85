#!/usr/bin/env python3
"""Execute actual AFE fragment/session code and complete-frame gate with mocked I/O."""
from pathlib import Path
import re, os, shlex, subprocess, tempfile
from bms_diag_host_check import function
ROOT=Path(__file__).resolve().parents[1]
MOD=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
def main():
    strip=lambda name:re.sub(r'^#include[^\n]*','',(MOD/name).read_text(encoding='utf-8'),flags=re.M)
    code=(ROOT/'tests/fixtures/afe_hw_fragments.c').read_text(encoding='utf-8')
    code=code.replace('/* HEADER */',strip('bms_afe_hw_access.h'))
    code=code.replace('/* ACCESS */',strip('bms_afe_hw_access.c'))
    gate=function((MOD/'modbus_rtu.c').read_text(encoding='utf-8'),'u8 bms_afe_hw_write_complete_frame(')
    code=code.replace('/* GATE */',gate)
    with tempfile.TemporaryDirectory(prefix='afe-fragments-') as d:
        src=Path(d)/'test.c';exe=Path(d)/'test.exe';src.write_text(code,encoding='utf-8')
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__': main()
