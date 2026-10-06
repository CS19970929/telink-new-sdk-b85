"""SH原厂SCT完整四位表、自清命令位与配置失败后FET门禁。"""
import os
from validation_support import read, profile_prefix, without_includes, run_c, evidence
product=os.environ.get('BMS_PRODUCT','d014')
code=profile_prefix(product)+'\n#include "sh3673520.h"\n#include "sh3673510_control.h"\n#include "sh3673510_ntc.h"\n'
for port in 'ABCD':
    for pin in range(8):code+=f'#define GPIO_P{port}{pin} {(ord(port)-65)*8+pin}\n'
fixture=read('tests/fixtures/sh_register_scenarios.c').split('int main(void)')[0]
fixture=fixture.replace('*v=regs[r];if(operation==corrupt_at)',
    'if(r==0x42)regs[r]&=0xfe; /* p16 TRG异步自清。 */\n    *v=regs[r];if(operation==corrupt_at)')
code+=fixture.replace('/* CONTROL */',without_includes(read('bms/afe/sh3673510/sh3673510_control.c')))
code+=r'''
int main(void) {
 /* 独立抄录原件p37–38枚举，未读取实现表生成期望。 */
 const unsigned oracle[]={0,32,64,96,128,192,224,256,288,320,384,448,480,512,544,576};
 bms_afe_hw_profile_build_default(&requested);reset_bus(0,0);
 assert(sh3673510_control_init());
 for(unsigned req=0;req<=576;++req) {
  operation=0;previous_write=0; /* 总线夹具逐次事务计数，避免256槽数组越界。 */
  requested.sc_delay_us=req;assert(sh3673510_control_apply_protection());
  unsigned expected=0;while(oracle[expected]<req)++expected;
  assert((regs[0x4f]&15u)==expected);
  sh3673510_protection_actual_t a;assert(sh3673510_control_get_protection_actual(&a));
  assert(a.sc_delay_us==oracle[expected]);
 }
 uint8_t changed;regs[0x42]=0x47;
 assert(sh3673510_control_set_load_detection(1,&changed));assert(changed && regs[0x42]==0x4a);
 /* 稳定owned位损坏必须继续拒绝；自清位不作为持久配置比较。 */
 corrupt_at=operation+3;regs[0x42]=0x44;
 assert(!sh3673510_control_set_load_detection(1,&changed));corrupt_at=0;
 operation=0;fail_at=1;assert(!sh3673510_control_apply_protection());fail_at=0;
 assert(!sh3673510_control_set_fets(1,1));assert(sh3673510_control_set_fets(0,0));
 assert(sh3673510_control_apply_protection());assert(sh3673510_control_set_fets(1,1));
 puts("PASS 577 SCT请求原厂解码、自清CRLD、配置失败FET拒绝与已验证恢复");return 0;
}
'''
out=run_c(code,['bms/afe/sh3673510/sh3673510_ntc.c'],['-Wno-unused-parameter','-Wno-unused-function'],name='sh-factory-registers')
evidence({'domain':'afe_factory_registers','product':product,'trace':out.strip(),'boundary':'完整control函数体与原NTC表；寄存器总线替身，不是芯片仿真'})
