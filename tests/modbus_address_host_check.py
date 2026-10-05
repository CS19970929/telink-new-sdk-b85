"""Run the production address allow-list and cached SH threshold mapping."""
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import os,re,shlex,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
paths=Sources(ROOT)
src=selected_source(paths/"modbus_rtu.c")
def function(name):
    match=re.search(r"(?m)^static (?:int|u16) "+name+r"\([^;]*?\)\s*\{",src)
    assert match,name
    pos=src.index('{',match.start())+1;depth=1
    while depth:
        depth+=(src[pos]=='{')-(src[pos]=='}');pos+=1
    return src[match.start():pos]
constants="\n".join(re.findall(r"(?m)^#define (?:BMS_AFE_ACTUAL_REG_(?:BASE|COUNT)|BMS_REALTIME_REG_(?:BASE|COUNT))[^\n]*",src))
constants+='\n'+'\n'.join(re.findall(r'(?m)^#define BTNAME_REG_(?:BASE|COUNT)\s+[^\n]*',(paths/'btname_modbus.h').read_text()))
is_sh=paths.product!="d008"
fixture=r'''
#include <stdint.h>
#include <assert.h>
typedef uint16_t u16;typedef uint8_t u8;
static int bms_parameter_readable(u16 r){(void)r;return 0;}
static int afe_hw_profile_is_reg(u16 r){(void)r;return 0;}
static int dvc_comm_is_semantic(u16 r){(void)r;return 0;}
static int dvc_comm_is_raw(u16 r){(void)r;return 0;}
/* CONSTANTS */
/* PRODUCTION */
int main(void){
 assert(read_address_supported(0xD000));assert(read_address_supported(0xD03E));
 assert(!read_address_supported(0xD03F));assert(read_address_supported(0x2140));
 assert(!read_address_supported(0x2141));assert(!read_address_supported(0xFFFF));
 assert(read_address_supported(BTNAME_REG_BASE));
 assert(read_address_supported(BTNAME_REG_BASE+BTNAME_REG_COUNT-1));
 assert(!read_address_supported(BTNAME_REG_BASE+BTNAME_REG_COUNT));
 /* SH_TEST */
 return 0;
}
'''
body=function('read_address_supported')
if is_sh:
    control=(paths/'sh3673510_control.h').read_text()
    struct=re.search(r'typedef struct\s*\{[^}]+\}\s*sh3673510_protection_actual_t;',control).group()
    stub=struct+'''\nstatic u8 actual_valid;
static u8 sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *a){
 a->ov_mv=3650;a->uv_mv=2700;a->ocd1_a10=111;a->ocd2_a10=222;a->occ_a10=333;
 a->ov_delay_ms=100;a->uv_delay_ms=200;a->ocd1_delay_ms=300;a->ocd2_delay_ms=400;a->occ_delay_ms=500;
 return actual_valid;}
'''
    body=stub+body+'\n'+function('read_afe_actual_reg')
    checks='''assert(read_address_supported(0x2180));assert(read_address_supported(0x218A));
 assert(!read_address_supported(0x218B));assert(read_afe_actual_reg(0x2180)==0);
 assert(read_afe_actual_reg(0x2181)==0xFFFF);actual_valid=1;
 assert(read_afe_actual_reg(0x2180)==1);assert(read_afe_actual_reg(0x2181)==3650);
 assert(read_afe_actual_reg(0x2182)==2700);assert(read_afe_actual_reg(0x2183)==111);
 assert(read_afe_actual_reg(0x2184)==222);assert(read_afe_actual_reg(0x2185)==333);
 assert(read_afe_actual_reg(0x2186)==100);assert(read_afe_actual_reg(0x2187)==200);
 assert(read_afe_actual_reg(0x2188)==300);assert(read_afe_actual_reg(0x2189)==400);
 assert(read_afe_actual_reg(0x218A)==500);'''
else:
    checks='assert(!read_address_supported(0x2180));'
fixture=fixture.replace('/* CONSTANTS */',constants).replace('/* PRODUCTION */',body).replace('/* SH_TEST */',checks)
with tempfile.TemporaryDirectory(prefix='bms-address-') as directory:
    source=Path(directory)/'mapping.c';source.write_text(fixture,encoding='utf8');exe=Path(directory)/'mapping.exe'
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS production address bounds and existing SH read-only cached threshold mapping: '+paths.product)
