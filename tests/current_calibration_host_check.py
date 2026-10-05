"""真实整数校准与独立 int64 物理公式比较，覆盖符号、饱和和 K/B 边界。"""
from validation_support import read, function, run_c, evidence

source=read('bms/core/bms_config_store.c')
body=function(source,'static u32 current_scale_ppm(')+'\n'+function(source,'int32_t bms_config_calibrate_current(')
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
typedef uint32_t u32;
static int g_bms_config_ready=1,g_bms_config_user_valid=1;
static struct {struct {int32_t current_offset_ma;uint32_t current_gain_ppm;}user;}g_bms_config;
/* IMPLEMENTATION */
static unsigned checks;
static void compare(int32_t raw,int32_t offset,uint32_t gain){
 g_bms_config.user.current_offset_ma=offset;g_bms_config.user.current_gain_ppm=gain;
 int64_t delta=(int64_t)raw-offset;
 if(delta>INT32_MAX)delta=INT32_MAX;
 if(delta < -INT32_MAX)delta=-INT32_MAX;
 int64_t expected=delta*gain/1000000;
 if(expected>INT32_MAX)expected=INT32_MAX;
 if(expected < -INT32_MAX)expected=-INT32_MAX;
 int32_t actual=bms_config_calibrate_current(raw);++checks;
 if(actual!=expected){fprintf(stderr,"current raw_ma=%ld offset_ma=%ld gain_ppm=%lu expected=%ld actual=%ld\n",(long)raw,(long)offset,(unsigned long)gain,(long)expected,(long)actual);exit(1);}
}
int main(void){
 const int32_t raws[]={INT32_MIN,INT32_MIN+1,-1000000,-201,-200,-1,0,1,200,201,1000000,INT32_MAX};
 const int32_t offsets[]={-1000000,-1,0,1,1000000};
 const uint32_t gains[]={100000,999999,1000000,1000001,10000000};
 for(unsigned i=0;i<12;i++)for(unsigned j=0;j<5;j++)for(unsigned k=0;k<5;k++)compare(raws[i],offsets[j],gains[k]);
 uint32_t seed=20261005;
 for(unsigned i=0;i<100000;i++){
  seed=seed*1664525u+1013904223u;int32_t raw=(int32_t)(seed>>1);if(seed&1u)raw=-raw;
  seed=seed*1664525u+1013904223u;int32_t offset=(int32_t)(seed%2000001)-1000000;
  seed=seed*1664525u+1013904223u;compare(raw,offset,100000+seed%9900001);
 }
 for(int ready=0;ready<2;ready++)for(int valid=0;valid<2;valid++)if(!ready||!valid){
  g_bms_config_ready=ready;g_bms_config_user_valid=valid;
  if(bms_config_calibrate_current(INT32_MIN)!=INT32_MIN)return 1;
 }
 printf("seed=20261005 exact_formula_checks=%u\n",checks);return 0;
}
'''.replace('/* IMPLEMENTATION */',body)
output=run_c(code,name='current-calibration')
evidence({'domain':'current','observations':output.strip(), 'boundary':'生产 K/B 整数函数；不证明 AFE ADC 零点/精度或校准环境实际为零电流'})
