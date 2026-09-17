"""Execute the diagnostic core and actual Modbus ingress with side-effect spies."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
MOD=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
def function(source, signature):
    start=source.index(signature); opening=source.index('{',start); depth=1;end=opening+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]
def main():
    source=(MOD/'modbus_rtu.c').read_text(encoding='utf-8')
    ingress=function(source,'int modbus_on_frame(')
    fixture=(ROOT/'tests/fixtures/bms_diag.c').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='bms-diag-') as directory:
        p=Path(directory)/'test.c';p.write_text(fixture.replace('/* MODBUS */',ingress),encoding='utf-8')
        exe=Path(directory)/'test.exe'
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(MOD),str(p),str(MOD/'bms_diag.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
