#!/usr/bin/env python3
"""Execute production semantic stores + journal against byte-cut RAM Flash.
Parameter validators/default builders are explicit stubs here; their separate
contracts/target build cover actual product definitions, not this host harness.
"""
from pathlib import Path
from project_paths import Sources
import re, tempfile
from validation_support import run_c
ROOT=Path(__file__).resolve().parents[1]
MOD = Sources(ROOT)
def source(n):
    text = re.sub(r'^\s*#(?:include[^\n]*|pragma once)', '', (MOD/n).read_text(), flags=re.M)
    if n == 'bms_config_store.c':
        start=text.index('static const bms_protection_params_t s_default_protection')
        end=text.index('\n}',text.index('void bms_config_store_get_default_protect'))+2
        text=text[:start]+'void bms_config_store_get_default_protect(bms_protection_params_t *p){if(p)memset(p,0,sizeof(*p));}'+text[end:]
    return text
def main():
    param=source('bms_protection_params.h'); a=param.index('typedef struct {'); b=param.index('} bms_protection_params_t;',a)+len('} bms_protection_params_t;')
    macros=''
    feature_header=(MOD/'bms_features.h').read_text()
    macros+='\n'+'\n'.join(re.findall(r'^#define BMS_BALANCE_(?:ENABLE_DEFAULT|START_VOLTAGE_MV_DEFAULT|START_DELTA_MV_DEFAULT|STOP_DELTA_MV_DEFAULT|CELL_PLAUSIBLE_MIN_MV|SUSPECT_DELTA_MV)[^\n]*',feature_header,re.M))
    headers='\n'.join(source(n) for n in ['bms_timing.h','bms_soc_defs.h','bms_state_store.h','bms_soc.h','bms_afe_hw_profile.h','bms_config_store.h','bms_event_log.h','bms_storage_platform.h'])
    units='\n'.join(source(n) for n in ['bms_config_store.c','bms_state_store.c','bms_event_log.c','bms_parameters.c'])
    fixture=(ROOT/'tests/fixtures/d008_storage/stores.c').read_text()
    code=fixture.replace('/* PARAMETER_PROTOCOL */',source('bms_parameter_access.h')+'\n'+source('bms_parameter_access.c')).replace('/* MACROS */',macros).replace('/* TYPES */',param[a:b]+'\n'+headers).replace('/* PRODUCTION */',units)
    sources=[str((MOD/n).relative_to(ROOT)) for n in ('storage_record.c','bms_diag.c')]
    flags=['-Wno-unused-function','-include',str(MOD/'bms_diag.h')]
    run_c(code,sources=sources,flags=flags,name='semantic_stores')
    # 模拟 TC32 的 packed 结构布局；x86 允许非对齐访问，夹具显式检查生产对象地址。
    run_c(code,sources=sources,flags=[*flags,'-fpack-struct','-fshort-enums',
          '-Wno-error=address-of-packed-member'],name='semantic_stores_packed')
    with tempfile.TemporaryDirectory(prefix='d008-storage-') as d:
        # 历史只读编号变化不再触发恢复；仍编译同一套生产实现。
        policy = (MOD/'bms_parameter_policy.h').read_text()
        revisions = iter(range(101, 110))
        policy = re.sub(r'(#define BMS_UPDATE_\w+_REVISION)\s+\d+u',
                        lambda match: match[1]+' '+str(next(revisions))+'u', policy)
        (Path(d)/'bms_parameter_policy.h').write_text(policy)
        run_c(code,sources=sources,flags=['-I',d,*flags],name='semantic_stores_revisions')
    platform=(ROOT/'tests/fixtures/d008_storage/platform.c').read_text()
    platform=platform.replace('/* MACROS */',macros).replace('/* TYPES */',source('bms_storage_platform.h'))
    unit=source('bms_storage_platform_telink.c').split('const storage_port_t *bms_storage_platform_port(void)')[0]
    run_c(platform.replace('/* PRODUCTION */',unit),sources=[sources[1]],
          flags=['-Wno-unused-function','-include',str(MOD/'storage_port.h'),*flags[1:]],name='storage_platform')
if __name__=='__main__': main()
