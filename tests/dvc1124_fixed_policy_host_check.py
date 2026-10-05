"""Freeze the actual baseline RMW sequence, including failures at every bus access."""
from pathlib import Path
import hashlib,json,os,shlex,subprocess,tempfile
from bms_diag_host_check import function
from project_paths import host_includes
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'bms/afe/dvc1124/dvc1124.c').read_text(encoding='utf8')
body='\n'.join(function(source,'uint8_t '+name+'(') for name in
    ('DVC1124_EncodeCurrentWake','DVC1124_EncodeBodyDiode','DVC1124_EncodeI2cWatchdog','DVC1124_ApplyProjectOperatingConfig'))
# The noinline prefix has no effect on the byte/transaction contract.
body += '\n'+function(source,'__attribute__((noinline)) uint8_t DVC1124_WriteRegisterSafe(')
special=(ROOT/'bms/afe/dvc1124/dvc1124_special.c').read_text(encoding='utf8')
body+='\nstatic uint8_t s_core_ot_event_latched;\n'+function(special,'static uint8_t dvc1124_read_core_ot_raw(')+'\n'+function(special,'uint8_t DVC1124_SetCoreOtThresholdCode(')
prefix=r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dvc1124.h"
static uint8_t regs[256];static unsigned operation,fail_at;
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t*data,uint8_t n){
 assert(n==1);++operation;printf("R:%02X:%02X:%u;",reg,regs[reg],operation==fail_at);
 if(operation==fail_at)return 0;*data=regs[reg];return 1;
}
uint8_t DVC1124_WriteRegisters(uint8_t reg,const uint8_t*data,uint8_t n){
 assert(n==1);++operation;printf("W:%02X:%02X:%u;",reg,*data,operation==fail_at);
 if(operation==fail_at)return 0;regs[reg]=*data;return 1;
}
"""
tail=r"""
int main(void){
 for(unsigned initial=0;initial<2;++initial)for(unsigned fail=0;fail<=45;++fail){
  memset(regs,initial?0xff:0,sizeof(regs));operation=0;fail_at=fail;
  unsigned result=DVC1124_ApplyProjectOperatingConfig();
  printf("=%u,%u\n",result,operation);
  assert(result==(fail==0));
 }
 return 0;
}
"""
def run(body,prefix=prefix,tail=tail):
 result={}
 for profile in (1,2,3):
  for hw in (0,1):
   with tempfile.TemporaryDirectory(prefix='dvc-fixed-') as folder:
    c=Path(folder)/'check.c';exe=Path(folder)/'check.exe';c.write_text(prefix+body+tail,encoding='utf8')
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
      '-DD008_PRODUCT_PROFILE=%d'%profile,'-DDVC1124_HW_PROTECT_ENABLE=%d'%hw,*host_includes(ROOT,'d008'),str(c),'-o',str(exe)],check=True)
    raw=subprocess.check_output([str(exe)]);result[str(profile)+'-'+str(hw)]=hashlib.sha256(raw).hexdigest()
 return result
if __name__=='__main__':
 expected=json.loads((ROOT/'tests/fixtures/dvc_fixed_6604d738.json').read_text(encoding='utf8'))
 assert run(body)==expected['hashes']
 print('PASS DVC fixed register order/reserved bits/failure ACKs: 3 profiles x HW on/off x 2 initial states x 46 bus-failure positions')
