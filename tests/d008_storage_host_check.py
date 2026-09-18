#!/usr/bin/env python3
"""Execute production semantic stores + journal against byte-cut RAM Flash.
Parameter validators/default builders are explicit stubs here; their separate
contracts/target build cover actual product definitions, not this host harness.
"""
from pathlib import Path
import re, subprocess, tempfile, os, shlex
ROOT=Path(__file__).resolve().parents[1]
MOD=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
def source(n):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)', '', (MOD/n).read_text(), flags=re.M)
def main():
    param=source('param.h'); a=param.index('struct PRT_E2ROM_PARAS {'); b=param.index('};',a)+2
    conf=(MOD/'conf.h').read_text()
    macros='\n'.join(re.findall(r'^#define (?:FW_UPGRADE_RESET_\w+|BMS_(?:STATE_SAVE|EVENT_SAVE|STORAGE_RETRY)_INTERVAL_32K)[^\n]*',conf,re.M))
    headers='\n'.join(source(n) for n in ['bms_soc_defs.h','bms_state_store.h','soc_kv_store.h','SocEnhance.h','bms_afe_hw_profile.h','bms_config_store.h','bms_cold_kv_store.h','bms_event_log.h','bms_storage_platform.h'])
    units='\n'.join(source(n) for n in ['bms_config_store.c','bms_state_store.c','bms_event_log.c','param.c'])
    fixture=(ROOT/'tests/fixtures/d008_storage/stores.c').read_text()
    code=fixture.replace('/* PARAMETER_PROTOCOL */',source('bms_parameter_access.c')).replace('/* MACROS */',macros).replace('/* TYPES */',param[a:b]+'\n'+headers).replace('/* PRODUCTION */',units)
    with tempfile.TemporaryDirectory(prefix='d008-storage-') as d:
        p=Path(d)/'stores.c';p.write_text(code); exe=Path(d)/'stores'
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(MOD),str(p),str(MOD/'storage_record.c'),str(MOD/'bms_diag.c'),'-include',str(MOD/'bms_diag.h'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        platform=(ROOT/'tests/fixtures/d008_storage/platform.c').read_text()
        platform=platform.replace('/* MACROS */',macros).replace('/* TYPES */',source('bms_storage_platform.h'))
        unit=source('bms_storage_platform_telink.c').split('const storage_port_t *bms_storage_platform_port(void)')[0]
        p.write_text(platform.replace('/* PRODUCTION */',unit))
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-include',str(MOD/'storage_port.h'),str(p),str(MOD/'bms_diag.c'),'-include',str(MOD/'bms_diag.h'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__': main()
