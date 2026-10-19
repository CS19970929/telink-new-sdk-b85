"""常驻休眠协议：真实状态模块及 Modbus 入口，覆盖诊断关闭的开发/生产模式。"""
import re
from validation_support import ROOT, read, function, run_c

source=read('bms/core/modbus_rtu.c')
ingress='\n'.join(function(source, signature) for signature in (
    'static int modbus_finish_response(', 'static int modbus_read_holding(',
    'static int modbus_write_single(', 'static int modbus_write_multiple(',
    'int modbus_on_frame('))
constants='\n'.join(re.findall(r'^#define (?:BMS_PARAM_REG_\w+|BMS_MOS_CONTROL_(?:BASE|COUNT))[^\n]*',
    read('bms/core/bms_parameter_access.h')+'\n'+source,re.M))
fixture=read('tests/fixtures/bms_diag.c').split('int main(void){',1)[0]
fixture=fixture.replace('/* MODBUS */',constants+'\n'+ingress)
fixture+=r'''
static u32 wire32(const u8 *p) { return (u32)u16be(p)|((u32)u16be(p+2)<<16); }
int main(void) {
 u8 q[256],r[256],bytes[40];u32 n,l;
 tick=0xfffffff0u;
 /* 无需诊断初始化，开机即可读稳定头和 INIT。 */
 n=request(q,1,3,BMS_SLEEP_STATUS_BASE,BMS_SLEEP_STATUS_WORDS);
 assert(modbus_on_frame(q,n,r,&l)&&l==45u);
 assert(u16be(r+3)==0x534cu&&u16be(r+5)==1u&&u16be(r+7)==BMS_SLEEP_STATE_INIT);
 u32 sleep_request=n;
 bms_diag_init();
 n=request(q,1,3,BMS_DIAG_BASE,3u);
 assert(modbus_on_frame(q,n,r,&l));
 assert(u16be(r+3)==0x4447u&&u16be(r+5)==1u);
 assert(u16be(r+7)&BMS_SLEEP_STATUS_CAPABILITY);
#if !BMS_SERVICE_DIAG_ENABLE
 assert(u16be(r+7)==BMS_SLEEP_STATUS_CAPABILITY);
#endif
 bms_sleep_status_publish(BMS_SLEEP_REASON_LOW,0u,12000u,60000u,0u,1u);
 tick=32u;
 n=request(q,1,3,BMS_SLEEP_STATUS_BASE,BMS_SLEEP_STATUS_WORDS);assert(n==sleep_request);
 assert(modbus_on_frame(q,n,r,&l)&&l==45u);
 assert(u16be(r+7)==BMS_SLEEP_STATE_COUNTING&&u16be(r+9)==BMS_SLEEP_REASON_LOW);
 assert(u16be(r+11)==1u&&u16be(r+13)==0u);
 assert(wire32(r+19)==12000u&&wire32(r+23)==60000u&&wire32(r+27)==48000u);
 assert(wire32(r+35)==0xfffffff0u&&wire32(r+39)==32u);
 assert((u32)(wire32(r+39)-wire32(r+35))==48u);
 /* 读和诊断初始化均不能重置、推进 PM 已发布的倒计时。 */
 bms_diag_init();
 for(unsigned i=0;i<50u;i++) {
  tick+=32u;assert(modbus_on_frame(q,n,r,&l));
  assert(wire32(r+19)==12000u&&wire32(r+27)==48000u);
  assert(wire32(r+35)==0xfffffff0u&&wire32(r+39)==tick);
 }
 bms_sleep_status_publish(BMS_SLEEP_REASON_OVERDISCHARGE,0u,179000u,180000u,0u,0u);
 assert(modbus_on_frame(q,n,r,&l)&&wire32(r+27)==1000u&&u16be(r+9)==8u);
 bms_sleep_status_publish(BMS_SLEEP_REASON_AFE,0u,UINT32_MAX,1800000u,0u,0u);
 assert(modbus_on_frame(q,n,r,&l)&&u16be(r+7)==BMS_SLEEP_STATE_READY&&wire32(r+27)==0u);
 bms_sleep_status_publish(BMS_SLEEP_REASON_COMMAND,BMS_SLEEP_BLOCK_UART_TX,500u,500u,3000u,0u);
 assert(modbus_on_frame(q,n,r,&l)&&u16be(r+7)==BMS_SLEEP_STATE_BLOCKED);
 assert(wire32(r+15)==BMS_SLEEP_BLOCK_UART_TX&&wire32(r+31)==3000u);
 bms_sleep_status_committed();
 assert(modbus_on_frame(q,n,r,&l)&&u16be(r+7)==BMS_SLEEP_STATE_COMMITTED);
 assert(!wire32(r+15)&&wire32(r+19)==500u&&!wire32(r+27)&&!wire32(r+31));
 bms_sleep_status_publish(BMS_SLEEP_REASON_NONE,BMS_SLEEP_BLOCK_OTA,0u,0u,0u,0u);
 assert(modbus_on_frame(q,n,r,&l)&&u16be(r+7)==BMS_SLEEP_STATE_NONE);
 assert(wire32(r+15)==BMS_SLEEP_BLOCK_OTA&&!wire32(r+27));
 assert(!bms_sleep_status_read(BMS_SLEEP_STATUS_BASE,0u,bytes));
 assert(!bms_sleep_status_read(BMS_SLEEP_STATUS_BASE,1u,0));
 assert(!bms_sleep_status_read(BMS_SLEEP_STATUS_BASE,126u,bytes));
 assert(!bms_sleep_status_read(0xffffu,2u,bytes));
 assert(!bms_sleep_status_overlaps(0xffffu,2u));
 assert(!bms_sleep_status_overlaps(BMS_SLEEP_STATUS_BASE,0u));
 assert(bms_sleep_status_overlaps(BMS_SLEEP_STATUS_BASE-1u,2u));
 for(unsigned end=0;end<2;end++) {
  u16 start=end?BMS_SLEEP_STATUS_END-1u:BMS_SLEEP_STATUS_BASE-1u;
  n=request(q,1,3,start,2u);assert(modbus_on_frame(q,n,r,&l)&&r[2]==2u);
  n=request(q,1,16,start,2u);assert(modbus_on_frame(q,n,r,&l)&&r[2]==2u);
 }
 for(unsigned addr=0;addr<2;addr++) for(unsigned func=6;func<=16;func+=10) {
  n=request(q,(u8)addr,(u8)func,BMS_SLEEP_STATUS_BASE,1u);
  assert(modbus_on_frame(q,n,r,&l)==(addr!=0u)&&r[2]==2u);
 }
 n=request(q,0,3,BMS_SLEEP_STATUS_BASE,20u);assert(!modbus_on_frame(q,n,r,&l));
 assert(reads==0u&&writes==0u);
 puts("PASS sleep status: startup, legacy discovery, countdown, read-only, side effects, clock wrap");
 return 0;
}
'''
for mode,defines in (
    ('development',[]),('production',['-DBMS_PRODUCTION_BUILD=1']),
    ('service',['-DBMS_SERVICE_DIAG_ENABLE=1'])):
    run_c(fixture,['bms/core/bms_sleep_status.c','bms/core/bms_diag.c','bms/core/bms_debug_log.c'],
        ['-Wno-unused-parameter','-I',str(ROOT/'tests/fixtures/d014_safety_loop/include'),*defines],
        name='sleep-status-'+mode)
