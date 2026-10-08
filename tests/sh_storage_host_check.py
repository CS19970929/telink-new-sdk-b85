"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sh_event_checkpoint_host_check():
    print("CHECK sh_event_checkpoint_host_check", flush=True)
    """Exercise SH production event checkpoint code; Flash journal has separate tests."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import tempfile,subprocess,os,shlex,re
    ROOT=Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    def source(n):return re.sub(r'^\s*#(?:include[^\n]*|pragma once)','',(APP/n).read_text(),flags=re.M)
    code=r"""
    #include <stdint.h>
    #include <string.h>
    #include <assert.h>
    typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
    #include "storage_record.h"
    #define BMS_ERROR_EEPROM_STORE 1
    static u32 tick, saves, blocked;
    static int save_ok=1;
    static u8 durable[404];
    static u32 pm_get_32k_tick(void){return tick;}
    static void bms_error_raise(int e){(void)e;}
    static void bms_diag_attempt(int d){(void)d;}
    static void bms_diag_result(int d,int e){(void)d;(void)e;}
    #define DIAG_INVALID 8
    #define DIAG_PORT 1
    #define DIAG_REGION 2
    #define DIAG_OPEN 3
    #define DIAG_DEFAULTS 4
    #define DIAG_OK 0
    #define DIAG_BACKOFF 12
    #define DIAG_SAVE 7
    #define BMS_PRODUCT_ID 14u
    #include "bms_update_policy.h"
    /* TYPES */
    static const storage_port_t port={0};
    const storage_port_t*bms_storage_platform_port(void){return &port;}
    int bms_storage_platform_region(bms_storage_domain_t d,storage_region_t*r){(void)d;r->base=4096;r->size=8192;return 1;}
    int storage_record_open(storage_record_store_t*s,const storage_port_t*p,storage_region_t r,uint32_t m,uint16_t v,uint16_t n){(void)s;(void)p;(void)r;(void)m;assert(v==3 && n==404);return 1;}
    int storage_record_load(storage_record_store_t*s,uint8_t*p){(void)s;(void)p;return 0;}
    int storage_record_save(storage_record_store_t*s,const uint8_t*p){(void)s;++saves;if(!save_ok){++blocked;return 0;}memcpy(durable,p,404);return 1;}
    /* PRODUCTION */
    int main(void){
     bms_event_log_sample_t sample={0};u16 pos;
     assert(bms_event_log_init());bms_event_log_note_startup();
     for(tick=0;tick<60u*32000u;tick+=32000u){sample.vcell_ovp^=1;bms_event_log_poll_1s(&sample);}
     assert(saves==1 && g_bms_event_log.dirty);
     bms_event_log_poll_1s(&sample);assert(saves==2 && !g_bms_event_log.dirty);
     pos=g_bms_event_log.write_pos;assert(bms_event_log_init() && g_bms_event_log.write_pos==pos);
     save_ok=0;sample.vcell_uvp=1;tick+=60u*32000u;bms_event_log_poll_1s(&sample);
     assert(saves==3 && blocked==1 && g_bms_event_log.dirty);
     pos=g_bms_event_log.write_pos;
     for(int i=0;i<4;++i){tick+=32000u;bms_event_log_poll_1s(&sample);}
     assert(saves==3 && g_bms_event_log.write_pos==pos);
     tick+=32000u;save_ok=1;bms_event_log_poll_1s(&sample);assert(saves==4 && !g_bms_event_log.dirty);
     bms_event_log_note_sleep();assert(saves==5);pos=g_bms_event_log.write_pos;
     bms_event_log_note_sleep();assert(saves==5 && pos==g_bms_event_log.write_pos);
     bms_event_log_cancel_sleep();bms_event_log_note_sleep();assert(saves==6);
     sample.vbus_ovp=1;bms_event_log_poll_1s(&sample);save_ok=0;
     pos=g_bms_event_log.write_pos;
     tick=UINT32_MAX-2u*32000u;g_bms_event_log.last_attempt_32k=tick;
     tick+=60u*32000u;save_ok=1;bms_event_log_poll_1s(&sample);assert(!g_bms_event_log.dirty);
     assert(pos==g_bms_event_log.write_pos);
     return 0;
    }
    """
    code=code.replace('/* TYPES */',source('bms_storage_platform.h')+source('bms_event_log.h')).replace('/* PRODUCTION */',source('bms_event_log.c'))
    with tempfile.TemporaryDirectory(prefix='sh-event-') as d:
     p=Path(d)/'event.c';p.write_text(code);exe=Path(d)/'event.exe'
     subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',*host_includes(ROOT),str(p),'-o',str(exe)],check=True)
     subprocess.run([str(exe)],check=True)
    print('PASS event storm batching, pending retention, retry/wrap, idempotent init, sleep cancellation and pending retry')

def check_sh_storage_platform_host_check():
    print("CHECK sh_storage_platform_host_check", flush=True)
    """Execute production SH Flash exclusion, retry and readback diagnostics with mocks."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import re,tempfile,subprocess,os,shlex
    ROOT=Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    def source(n):return re.sub(r'^\s*#(?:include[^\n]*|pragma once)','',(APP/n).read_text(),flags=re.M)
    fixture=(ROOT/'tests/fixtures/sh_storage_platform.c').read_text()
    fixture=fixture.replace('/* TYPES */',source('bms_storage_platform.h')).replace('/* MACROS */','')
    unit=source('bms_storage_platform_telink.c').split('const storage_port_t *bms_storage_platform_port(void)')[0]
    fixture=fixture.replace('/* PRODUCTION */',unit)
    with tempfile.TemporaryDirectory(prefix='sh-platform-') as d:
     p=Path(d)/'platform.c';p.write_text(fixture);exe=Path(d)/'platform.exe'
     subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-include',str(APP/'storage_port.h'),'-include',str(APP/'bms_diag.h'),str(p),'-o',str(exe)],check=True)
     subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    check_sh_event_checkpoint_host_check()
    check_sh_storage_platform_host_check()
