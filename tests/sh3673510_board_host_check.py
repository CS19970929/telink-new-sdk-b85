"""Execute the selected SH board init and the actual sample-to-protection mapping."""
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = Sources(ROOT)
product = os.environ.get('BMS_PRODUCT', 'd014')
app = selected_source(APP / 'app.c')
start = app.index('static void board_init(void)')
board = app[start:app.index('\n}\n', start) + 3]
source = selected_source(APP / 'sh3673510_bms.c')
start = source.index('        memset(&sw, 0, sizeof(sw));')
end = source.index('        bms_sw_protection_update(&sw);', start)
mapping = source[start:end]
expected = int(product != 'd013')
code = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "bms_sw_protection.h"
#include "bms_sh3673510_config.h"
#include "sh3673520_reg.h"
#define AS_GPIO 1
#define GPIO_PA0 0
#define GPIO_PD3 3
#define GPIO_PD4 4
static int function[8], input[8], output[8], value[8], output_inhibit;
static void bms_afe_set_output_enabled(int enabled){output_inhibit=!enabled;}
static void gpio_set_func(int p,int v){function[p]=v;}
static void gpio_write(int p,int v){value[p]=v;}
static void gpio_set_input_en(int p,int v){input[p]=v;}
static void gpio_set_output_en(int p,int v){
 if(p==GPIO_PD4 && v)assert(function[p]==AS_GPIO && value[p]==1 && input[p]==0);
 output[p]=v;
}
''' + board + r'''
#define MOS_TEMP1 3
static struct {uint16_t u16Temperature[4];} g_stCellInfoReport;
static uint8_t s_ntc_valid[4];
static uint8_t s_sample_pending, s_charger_removed, s_load_removed;
static struct {uint8_t bstatus2;} status;
static uint8_t battery_temperature_snapshot(uint16_t *low,uint16_t *high){*low=*high=650;return 1;}
static bms_sw_protection_inputs_t sample(void){bms_sw_protection_inputs_t sw;
''' + mapping + r'''
 return sw;
}
int main(void){
 for(int i=0;i<8;++i)function[i]=input[i]=output[i]=value[i]=-1;
 board_init();
 assert(output_inhibit);
 assert(function[GPIO_PD4]==AS_GPIO && value[GPIO_PD4]==1);
 assert(input[GPIO_PD4]==0 && output[GPIO_PD4]==1);
 assert(input[GPIO_PA0]==1 && output[GPIO_PA0]==0);
 assert(input[GPIO_PD3]==1 && output[GPIO_PD3]==0);
 for(int valid=0;valid<=1;++valid){
  s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX]=valid;
  g_stCellInfoReport.u16Temperature[MOS_TEMP1]=1350;
  bms_sw_protection_inputs_t in=sample();
  assert(in.battery_temp_valid && in.battery_temp_min==650);
  assert(in.mos_temp_required==EXPECTED_MOS);
  assert(in.mos_temp_valid==(EXPECTED_MOS && valid));
  assert(in.mos_temp==((EXPECTED_MOS && valid)?1350:0));
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sh-board-') as d:
    c=Path(d)/'check.c';c.write_text(code);exe=Path(d)/'check'
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',
        '-DEXPECTED_MOS=%d'%expected,*host_includes(ROOT),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS '+product+' communication power GPIO sequencing and MOS capability/validity mapping')
