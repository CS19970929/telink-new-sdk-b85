"""原始22 TU验证：配置失败只可经完整回滚/重配解除，健康ADC不是配置证据。"""
from validation_support import ROOT, read, run_c, evidence
from d014_safety_loop_host_check import SOURCES

fixture=read('tests/fixtures/d014_safety_loop/loop.c').split('int main(void)')[0]
fixture=fixture.replace('static int corrupt_read_reg = -1;',
    'static int corrupt_read_reg = -1;\nstatic int corrupt_remaining;')
fixture=fixture.replace('if (corrupt_read_reg >= frame[1] && corrupt_read_reg < (int)frame[1]+tx)\n            reply[corrupt_read_reg-frame[1]] ^= 0x80u;',
    'if (corrupt_remaining && corrupt_read_reg >= frame[1] && corrupt_read_reg < (int)frame[1]+tx) {\n'
    ' if(corrupt_remaining>0)--corrupt_remaining;reply[corrupt_read_reg-frame[1]] ^= 0x80u;\n}')
fixture+=r'''
#include "bms_afe_hw_access.h"
#include "bms_crc.h"
static void open_access(void) {
 uint8_t req[9]={1,BMS_AFE_HW_ACCESS_MODBUS_FUNC,BMS_AFE_HW_ACCESS_CMD_OPEN},rsp[32];
 uint32_t len=0,magic=BMS_AFE_HW_ACCESS_UNLOCK_MAGIC;
 req[3]=magic>>24;req[4]=magic>>16;req[5]=magic>>8;req[6]=magic;
 uint16_t crc=mb_crc16(req,7);req[7]=crc;req[8]=crc>>8;
 assert(bms_afe_hw_access_modbus_on_frame(req,9,rsp,&len));assert(bms_afe_hw_access_is_active());
}
int main(void) {
 memset(flash,255,sizeof(flash));
 for(unsigned i=0;i<20;++i)raw16((uint8_t)(0x69+2*i),21120);
 for(unsigned i=0;i<4;++i)raw16((uint8_t)(0x5d+2*i),16384);
 raw16(0x93,6758);
 bms_diag_init();bms_parameters_startup();bms_parameters_init();bms_afe_init();request_outputs();steps(12);
 assert(bms_afe_samples_qualified() && discharge_on());
 bms_afe_hw_profile_t before,candidate,after;
 assert(bms_afe_hw_profile_get(&before));candidate=before;candidate.cov_mv+=100;candidate.enable_mask=0;
 uint8_t payload[70];for(unsigned i=0;i<35;++i){uint16_t w;memcpy(&w,(uint8_t*)&candidate+i*2,2);payload[i*2]=w>>8;payload[i*2+1]=w;}
 open_access();corrupt_read_reg=SH3673520_REG_OVL;
 const char *scenario=getenv("GATE_CASE");assert(scenario);
 int persistent=!strcmp(scenario,"rollback-failure");corrupt_remaining=persistent?-1:1;
 unsigned error=bms_afe_hw_profile_commit_be(payload,35);
 corrupt_read_reg=-1;corrupt_remaining=0;
 assert(bms_afe_hw_profile_get(&after) && !memcmp(&before,&after,sizeof(before)));
 assert(!discharge_on());
 if(!persistent) {
  assert(error==BMS_AFE_HW_ERROR_APPLY_VERIFY);
  assert(bms_afe_hw_profile_apply_state()==BMS_AFE_HW_APPLY_ROLLBACK_OK);
  assert(bms_afe_hw_profile_get_effective(&after));
  assert(after.cov_mv==((before.cov_mv+2u)/5u)*5u);
  steps(12);assert(bms_afe_samples_qualified() && discharge_on());
 } else {
  assert(error==BMS_AFE_HW_ERROR_ROLLBACK);
  assert(bms_afe_hw_profile_apply_state()==BMS_AFE_HW_APPLY_INCONSISTENT);
  /* 40秒健康采样，包含现有35秒通信恢复/AFE_INIT窗口，也不能解除配置门禁。 */
  for(unsigned i=0;i<200;++i){step();assert(!discharge_on() && !bms_afe_samples_qualified());}
  bms_afe_init();request_outputs();steps(12);
  assert(bms_afe_hw_profile_get_effective(&after) && bms_afe_samples_qualified() && discharge_on());
 }
 printf("PASS 配置门禁场景=%s error=%u；只有完整验证后允许输出\n",scenario,error);return 0;
}
'''
assert set(SOURCES)<=set(read('bms/products/d014/sources.txt').splitlines())
out=run_c(fixture,SOURCES,['-I',str(ROOT/'tests/fixtures/d014_safety_loop/include'),'-Wno-misleading-indentation','-Wno-unused-function'],
          name='afe-configuration-gate',runs=[{'GATE_CASE':c} for c in ('rollback-success','rollback-failure')])
evidence({'domain':'configuration_gate','production_units':SOURCES,'observations':out.strip(),
          'boundary':'22原始TU；SPI/Flash/时钟替身，观察寄存器命令，不代表实板MOS'})
