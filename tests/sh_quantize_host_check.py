"""Exhaust all requested current words against an independent 64-bit oracle."""
from pathlib import Path
import os, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
code=(ROOT/'bms/afe/sh3673510/sh3673510_quantize.h').read_text(encoding='utf8')+r'''
#include <assert.h>
int main(void){
 const uint32_t shunts[]={100,250,667,2000},parallel[]={1,1,1,3},steps[]={5000,10000,1375};
 const uint8_t maxima[]={15,15,31};
 for(unsigned r=0;r<4;++r)for(unsigned t=0;t<3;++t)for(uint32_t req=0;req<=65535;++req){
  uint64_t denominator=(uint64_t)steps[t]*10u*parallel[r];
  uint64_t numerator=(uint64_t)req*shunts[r];
  uint64_t count=(numerator+denominator-1u)/denominator;
  if(!count)count=1;
  if(count>maxima[t]+1u)count=maxima[t]+1u;
  uint64_t expected=(count*steps[t]*10u*parallel[r]+shunts[r]-1u)/shunts[r];
  if(expected>65535u)expected=65535u;
  uint8_t encoded=255;
  assert(sh3673510_quantize_current_ratio_a10((uint16_t)req,shunts[r],parallel[r],steps[t],maxima[t],&encoded)==expected);
  assert(encoded==count-1u);
 }
 assert(!sh3673510_quantize_current_a10(1,0,5000,15,0));
 assert(!sh3673510_quantize_current_a10(1,667,0,15,0));
 uint8_t code=255;
 assert(sh3673510_quantize_current_ratio_a10(150,2000,3,10000,15,&code)==150 && code==0);
 assert(sh3673510_quantize_current_a10(150,667,10000,15,&code)==300 && code==1);
 assert(sh3673510_quantize_current_ratio_a10(75,2000,3,5000,15,&code)==75 && code==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sh-quantize-') as folder:
    c=Path(folder)/'check.c';exe=Path(folder)/'check.exe';c.write_text(code,encoding='utf8')
    for opt in ('-O2','-Os'):
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99',opt,'-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('PASS 786432 threshold/code pairs per optimization: D011/D013/D014 shunts, all input words and current ranges')
