"""Exercise actual name storage and protection filter code with failure sequences."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from validation_support import function, run_c

ROOT = Path(__file__).resolve().parents[1]


def run(source, defines=()):
    with tempfile.TemporaryDirectory(prefix='bms-simplification-') as directory:
        src = Path(directory) / 'test.c'
        exe = Path(directory) / 'test.exe'
        src.write_text(source, encoding='utf-8')
        command = shlex.split(os.environ.get('CC', 'cc'))
        subprocess.run(command + ['-std=c99', '-Wall', '-Wextra', '-Werror',
                                 *defines, str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def name_test():
    source = (ROOT / 'bms/core/btname_modbus.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
    header = (ROOT / 'bms/core/btname_modbus.h').read_text(encoding='utf-8')
    prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8;
#define BMS_AFE_BACKEND_DVC1124 1
#define BMS_AFE_BACKEND_SH3673510 2
#define BMS_ERROR_EEPROM_STORE 12
static int errors, save_ok, saves, broadcasts;
u8 my_devName[BTNAME_TOTAL_MAX_LEN];
static void bms_error_raise(int e){assert(e==BMS_ERROR_EEPROM_STORE);++errors;}
static int bms_config_store_get_bt_name_suffix(char*s,unsigned n){assert(n>7);strcpy(s,"DEFAULT");return 1;}
static int bms_config_store_set_bt_name_suffix(const char*s){assert(strcmp(s,"NEW")==0);++saves;return save_ok;}
static void bls_ll_setAdvEnable(int n){(void)n;}
static void bls_ll_setScanRspData(uint8_t*s,unsigned n){assert(n>=2&&n<=31&&s[1]==9);++broadcasts;}
'''
    tail = r'''
int main(void){
    uint16_t words[2]={0}; int previous;
    memcpy(words,"NEW",3); btname_init(); previous=broadcasts;
    assert(btname_modbus_on_write_holding(0x100,2,words)==0);
    assert(errors==1&&saves==1&&broadcasts==previous);
    assert(strcmp(btname_get(),"BT_DEFAULT")==0);
    assert(strcmp((char*)my_devName,"BT_DEFAULT")==0);
    save_ok=1;
    assert(btname_modbus_on_write_holding(0x100,2,words)==1);
    assert(strcmp(btname_get(),"BT_NEW")==0);
    assert(strcmp((char*)my_devName,"BT_NEW")==0);
    assert(saves==2&&broadcasts==previous+1);
    assert(btname_modbus_on_write_holding(0x100,2,words)==1);
    assert(saves==2&&broadcasts==previous+1);
    assert(btname_modbus_on_write_holding(0x100,0,0)==(BMS_AFE_BACKEND==2));
    puts("name failure/success/retry/cache: PASS"); return 0;
}
'''
    for backend in (1, 2):
        run(header + prefix + source + tail, [f'-DBMS_AFE_BACKEND={backend}'])


def filter_test():
    source = (ROOT / 'bms/core/bms_sw_protection.c').read_text(encoding='utf-8')
    prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define BMS_SW_PROTECTION_SAMPLE_MS 200u
typedef struct {uint16_t trip_count,recover_count;uint8_t active;} bms_sw_filter_t;
typedef enum {BMS_SW_HIGH=0,BMS_SW_LOW} bms_sw_direction_t;
'''
    funcs = '\n'.join(function(source, signature) for signature in (
        'static uint16_t bms_sw_filter_samples(',
        'static void bms_sw_filter_reset(',
        'static uint8_t bms_sw_filter_update('))
    tail = r'''
int main(void){
    bms_sw_filter_t s={0}; const uint16_t trip[]={110,110,90,110,110}; unsigned i;
    for(i=0;i<5;i++)assert(bms_sw_filter_update(&s,trip[i],100,80,60,BMS_SW_HIGH,1)==(i==4));
    assert(bms_sw_filter_update(&s,80,100,80,60,BMS_SW_HIGH,1)==1);
    assert(bms_sw_filter_update(&s,90,100,80,60,BMS_SW_HIGH,1)==1);
    assert(bms_sw_filter_update(&s,80,100,80,60,BMS_SW_HIGH,1)==1);
    assert(bms_sw_filter_update(&s,80,100,80,60,BMS_SW_HIGH,1)==1);
    assert(bms_sw_filter_update(&s,80,100,80,60,BMS_SW_HIGH,1)==0);
    puts("leaky trigger/continuous recovery: PASS");return 0;
}
'''
    run(prefix + funcs + tail)


def mos_request_test():
    # 验证共用应用请求的实际输出，避免冻结局部变量名或函数文本。
    source = (ROOT / 'bms/app/app.c').read_text(encoding='utf-8')
    run_c(r'''
#include <stdint.h>
#include <assert.h>
static struct { struct { uint8_t b1Status_Cool; } bits; } g_bms_system_status;
static unsigned calls;
static uint8_t bms_afe_set_fets(uint8_t c, uint8_t d) {
    assert(c == 1u && d == 1u); ++calls; return 0u;
}
''' + function(source, 'void mos_update(') + r'''
int main(void) {
    g_bms_system_status.bits.b1Status_Cool = 1u;
    mos_update();
    assert(calls == 1u && g_bms_system_status.bits.b1Status_Cool == 0u);
    mos_update(); assert(calls == 2u);
    return 0;
}
''', name='common-mos-request')


if __name__ == '__main__':
    name_test()
    filter_test()
    mos_request_test()
