"""SOC 曲线纯业务接口；持久化逐字节故障由 storage_host_check 的真实 Config+journal 覆盖。"""
from validation_support import run_c
code=r'''#include <assert.h>
#include <string.h>
#include "bms_soc_profile.h"
int main(void){
 bms_soc_profile_config_t c;
 assert(sizeof(c)==100u);
 for(unsigned chemistry=1;chemistry<=2;++chemistry){
  bms_soc_profile_defaults(chemistry,&c);assert(bms_soc_profile_valid(&c));
  for(unsigned i=0;i<50u;++i){uint16_t v=bms_soc_profile_word(&c,i);assert(bms_soc_profile_set_word(&c,i,v));}
  assert(!bms_soc_profile_set_word(&c,50u,1u));
  c.chemistry=257u;assert(!bms_soc_profile_valid(&c));
  bms_soc_profile_defaults(chemistry,&c);c.points[1].mv=c.points[0].mv;assert(!bms_soc_profile_valid(&c));
 }
 bms_soc_profile_defaults(255u,&c);assert(!bms_soc_profile_valid(&c));
 assert(!bms_soc_profile_valid(0));bms_soc_profile_defaults(1u,0);
 return 0;
}'''
run_c(code,['bms/core/bms_soc_profile.c'],name='soc-profile-business')
