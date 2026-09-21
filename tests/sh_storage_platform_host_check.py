"""Execute production SH Flash exclusion, retry and readback diagnostics with mocks."""
from pathlib import Path
import re,tempfile,subprocess,os,shlex
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
def source(n):return re.sub(r'^\s*#(?:include[^\n]*|pragma once)','',(APP/n).read_text(),flags=re.M)
fixture=(ROOT/'tests/fixtures/sh_storage_platform.c').read_text()
fixture=fixture.replace('/* TYPES */',source('bms_storage_platform.h')).replace('/* MACROS */','#define BMS_STORAGE_RETRY_INTERVAL_32K (5u*32000u)')
unit=source('bms_storage_platform_telink.c').split('const storage_port_t *bms_storage_platform_port(void)')[0]
fixture=fixture.replace('/* PRODUCTION */',unit)
with tempfile.TemporaryDirectory(prefix='sh-platform-') as d:
 p=Path(d)/'platform.c';p.write_text(fixture);exe=Path(d)/'platform.exe'
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-include',str(APP/'storage_port.h'),'-include',str(APP/'bms_diag.h'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
