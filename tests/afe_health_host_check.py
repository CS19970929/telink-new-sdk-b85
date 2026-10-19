"""真实 guard/State 的时间边界；SH 另串接实际后端、控制和 CRC 事务。"""
import os
from validation_support import read, run_c, ROOT, evidence

guard = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_afe_driver.h"
#include "bms_state.h"
#include "bms_error.h"
#include "bms_parameters.h"
#include "bms_features.h"
#include "bms_sw_protection.h"
#include "bms_afe_registers.h"
const bms_afe_driver_policy_t bms_afe_driver_policy = {5000u,0u,1u};
static uint32_t now;
static unsigned c,d,writes,samples,initializations,sleep_calls;
static unsigned set_calls, fail_next_control;
static uint8_t control_ok=1, balance_ok=1, balance_get_ok=1, balance_used_known, direction_allow_mask;
static bms_afe_sample_status_t status=BMS_AFE_SAMPLE_OK;
static int32_t measured_current;
#if BMS_AFE_REGISTER_DEBUG_ENABLE
static uint8_t config_ready=1;
static unsigned access_closes;
void bms_afe_hw_access_poll(void){}
void bms_afe_hw_access_close(void){++access_closes;}
uint8_t bms_afe_driver_register_config_ready(void){return config_ready;}
#endif
uint32_t bms_time_ms(void){return now;}
void bms_features_init(void){}
void bms_features_stop(void){}
void bms_sw_protection_init(void){}
void bms_sw_protection_reset_filters(void){}
uint8_t bms_protection_params_valid(void){return 1;}
void bms_afe_driver_init(void){++initializations;}
bms_afe_sample_status_t bms_afe_driver_sample(void){
 ++samples;bms_afe_sample_status_t r=status;return r;
}
uint8_t bms_afe_driver_set_fets(uint8_t a,uint8_t b){
 ++set_calls;
 if(fail_next_control){--fail_next_control;return 0;}
 if(!control_ok)return 0;
 if(status==BMS_AFE_SAMPLE_OK){if(c!=a||d!=b)++writes;c=a;d=b;}
 else {if(!a)c=0;if(!b)d=0;}return 1;
}
uint8_t bms_afe_driver_disable_fets(uint8_t a,uint8_t b){if(!control_ok)return 0;if(a)c=0;if(b)d=0;return 1;}
uint8_t bms_afe_driver_get_direction_allow_mask(void){return direction_allow_mask;}
uint8_t bms_afe_driver_get_aux_measurements(bms_afe_aux_measurements_t *m){
 if(m){memset(m,0,sizeof(*m));m->current_ma=measured_current;}return status==BMS_AFE_SAMPLE_OK;
}
uint8_t bms_afe_driver_get_feature_snapshot(bms_afe_feature_snapshot_t *s){(void)s;return 1;}
uint8_t bms_afe_driver_get_charge_source_present(uint8_t *p){*p=0;return 1;}
uint8_t bms_afe_driver_set_balance_mask(uint32_t m,const uint32_t *known_actual){(void)m;balance_used_known=(known_actual!=NULL);return balance_ok;}
uint8_t bms_afe_driver_get_balance_mask(uint32_t *m){if(!balance_get_ok)return 0;*m=0;return 1;}
uint8_t bms_afe_driver_apply_protection_config(void){return control_ok;}
uint8_t bms_afe_driver_sleep(void){++sleep_calls;return 1;}
static void poll(void){
 bms_afe_sample_status_t r=bms_afe_sample();
 if(r==BMS_AFE_SAMPLE_OK){if(!balance_ok)(void)bms_afe_set_balance_mask(1u, NULL);bms_afe_apply_outputs();}
}
static void qualify(void){
 status=BMS_AFE_SAMPLE_OK;control_ok=1;direction_allow_mask=0;bms_afe_init();
 bms_afe_set_channel_allowed(0,1);bms_afe_set_channel_allowed(1,1);
 bms_afe_set_fets(1,1);bms_afe_set_output_enabled(1);
 for(unsigned i=0;i<3;i++)poll();assert(c&&d&&!bms_error_get(BMS_ERROR_AFE1));
}
int main(void){
 bms_afe_aux_measurements_t m;uint8_t bits;
 /* 已有反向放行不能因读失败被撤销；失效期间的新关闭仍只作用于指定方向。 */
 for(unsigned channel=0;channel<2;channel++){
  qualify();direction_allow_mask=(uint8_t)(1u<<channel);
  assert(bms_afe_set_channel_allowed((uint8_t)channel,0)&&c&&d);
  status=BMS_AFE_SAMPLE_READ_ERROR;poll();assert(c&&d);
  assert(bms_afe_set_channel_allowed((uint8_t)(1u-channel),0));
  assert(c==(channel==0u)&&d==(channel==1u));
  status=BMS_AFE_SAMPLE_OK;direction_allow_mask=0;poll();assert(!c&&!d);
 }
 now=UINT32_MAX-500u;qualify();unsigned before=writes;
 /* 开启首次失败、补偿成功：不能第二次进入正常 set_fets，也不能丢掉失败观察。 */
 assert(bms_afe_set_channel_allowed(0,0)&&!c&&d);
 unsigned calls_before=set_calls;
 fail_next_control=1;
 assert(!bms_afe_set_channel_allowed(0,1));
 assert(set_calls==calls_before+1u && !c && d);
 assert(bms_afe_get_remote_allowed_mask()==2u);
 assert(bms_afe_set_channel_allowed(0,1)&&!c&&d);
 status=BMS_AFE_SAMPLE_OK;poll();assert(c&&d);
 before=writes;
 assert(bms_afe_get_aux_measurements(&m)&&!bms_afe_get_aux_measurements(NULL));
 status=BMS_AFE_SAMPLE_READ_ERROR;poll();
 assert(c&&d&&!bms_error_get(BMS_ERROR_AFE1));
 assert(!bms_afe_get_aux_measurements(&m)&&!bms_afe_get_fet_status(&bits));
 assert(!bms_afe_get_aux_measurements(NULL));
 now+=999;poll();assert(c&&d&&!bms_error_get(BMS_ERROR_AFE1));
 now+=1;poll();assert(!c&&!d&&bms_error_get(BMS_ERROR_AFE1));
 assert(!bms_afe_bus_access_allowed());unsigned calls=samples;
 for(unsigned i=0;i<100;i++)poll();assert(samples==calls);
 now+=5001;poll();assert(bms_afe_bus_access_allowed());
 status=BMS_AFE_SAMPLE_OK;poll();assert(c&&d&&!bms_error_get(BMS_ERROR_AFE1));
 /* 一秒确认不再叠加三次失败；两个失败观察也可确认。 */
 qualify();status=BMS_AFE_SAMPLE_READ_ERROR;poll();now+=1000u;poll();
 assert(bms_error_get(BMS_ERROR_AFE1)&&!bms_afe_bus_access_allowed());
 now+=5001u;status=BMS_AFE_SAMPLE_OK;poll();qualify();
 before=writes;
 for(unsigned i=0;i<20;i++){
  now+=200;status=BMS_AFE_SAMPLE_READ_ERROR;poll();assert(c&&d);
  now+=200;status=BMS_AFE_SAMPLE_OK;poll();assert(c&&d);
 }
 assert(writes==before&&!bms_error_get(BMS_ERROR_AFE1));
 status=BMS_AFE_SAMPLE_READ_ERROR;poll();
 assert(bms_afe_set_channel_allowed(0,0)&&!c&&d);
 assert(bms_afe_set_channel_allowed(0,1)&&!c&&d);
 status=BMS_AFE_SAMPLE_OK;poll();assert(c&&d);
 /* 读取成功不能掩盖反复失败的 MOS 事务。 */
 control_ok=0;poll();now+=500;poll();assert(c&&d);
 now+=500;poll();assert(bms_error_get(BMS_ERROR_AFE1)&&!bms_afe_bus_access_allowed());
 control_ok=1;now+=5001;poll();qualify();
 #if BMS_FEATURE_BALANCE_SUPPORTED
 /* 前置均衡读失败、OFF 成功也不能清掉观察或沿用旧资格。 */
 uint32_t actual_balance=0u;
 for(unsigned i=0;i<2;i++){
  assert(bms_afe_sample()==BMS_AFE_SAMPLE_OK);
  balance_get_ok=0;assert(!bms_afe_get_balance_mask(&actual_balance));balance_get_ok=1;
  assert(bms_afe_set_balance_mask(0u,&actual_balance)&&!balance_used_known);
  assert(!bms_afe_set_balance_mask(1u,&actual_balance));
  bms_afe_apply_outputs();
  if(!i){assert(!bms_error_get(BMS_ERROR_AFE1));now+=1000u;}
 }
 assert(bms_error_get(BMS_ERROR_AFE1)&&!bms_afe_bus_access_allowed());
 now+=5001u;poll();qualify();
 /* 均衡通信失败后，旧有效测量不能授权同轮新开；下一完整轮次恢复。 */
 assert(bms_afe_set_channel_allowed(0,0)&&!c&&d);
 balance_ok=0;poll();assert(!c&&d&&!bms_error_get(BMS_ERROR_AFE1));
 assert(bms_afe_set_channel_allowed(0,1)&&!c&&d);
 balance_ok=1;poll();assert(c&&d&&!bms_error_get(BMS_ERROR_AFE1));
 /* 本轮均衡必要事务失败，即使后续 MOS 成功也不能清零。 */
 balance_ok=0;poll();now+=999;poll();assert(!bms_error_get(BMS_ERROR_AFE1));
 now+=1;poll();assert(bms_error_get(BMS_ERROR_AFE1)&&!bms_afe_bus_access_allowed());
 balance_ok=1;now+=5001;poll();qualify();
 #endif
 status=BMS_AFE_SAMPLE_ERROR;poll();assert(!c&&!d&&bms_error_get(BMS_ERROR_AFE1));
 qualify();bms_afe_invalidate_configuration();assert(!c&&!d);
 for(unsigned i=0;i<20;i++)poll();assert(!c&&!d&&bms_error_get(BMS_ERROR_AFE1));
 #if BMS_AFE_REGISTER_DEBUG_ENABLE
 qualify();measured_current=201;
 assert(!bms_afe_register_debug_begin() && !bms_afe_register_debug_held() && c && d);
 measured_current=-201;assert(!bms_afe_register_debug_begin());measured_current=0;
 assert(bms_afe_set_channel_allowed(0,0));
 assert(bms_afe_register_debug_begin() && bms_afe_register_debug_held() && !c && !d);
 assert(!bms_afe_bus_access_allowed() && bms_afe_register_bus_access_allowed());
 unsigned frozen_samples=samples;
 for(unsigned i=0;i<10;i++){now+=1000;poll();assert(!c&&!d);}
 assert(samples==frozen_samples && !bms_afe_get_aux_measurements(&m));
 assert(!bms_afe_get_fet_status(&bits) && !bms_afe_apply_protection_config());
 config_ready=0;assert(!bms_afe_register_debug_restore() && bms_afe_register_debug_held());
 config_ready=1;control_ok=0;assert(!bms_afe_register_debug_restore() && bms_afe_register_debug_held());
 control_ok=1;assert(bms_afe_register_debug_restore() && !bms_afe_register_debug_held());
 assert(!c&&!d && bms_afe_get_remote_allowed_mask()==2u);
 poll();assert(!c&&d); /* 恢复仍保留远程关闭意图，并等待正常样本。 */
 /* 强制休眠接管冻结，不恢复配置或开MOS；失败仍撤销调试授权。 */
 for(unsigned restore_failed=0;restore_failed<2;restore_failed++){
  qualify();assert(bms_afe_register_debug_begin() && !c && !d);
  if(restore_failed){
   config_ready=0;assert(!bms_afe_register_debug_restore() && bms_afe_register_debug_held());
   config_ready=1;
  }
  unsigned init_before=initializations, samples_before=samples;
  unsigned closes_before=access_closes, sleeps_before=sleep_calls;
  c=d=1u; /* 模拟冻结期间芯片状态变化；强制接管仍须重新尝试关闭。 */
  assert(bms_afe_sleep()==!restore_failed);
  assert(!bms_afe_register_debug_held() && access_closes==closes_before+1u);
  assert(initializations==init_before && samples==samples_before && !c && !d);
  assert(sleep_calls==sleeps_before+(restore_failed?0u:1u));
  poll();assert(!c&&!d); /* 恢复通信也不能重新授权输出。 */
 }
 #endif
 puts("PASS guard：999/1000ms、tick回绕、瞬时/间歇失败无OFF、只关闭不新开、事务持续失败、静默和完整有效读取恢复、配置禁止");
 return 0;
}
'''
run_c(guard, ['bms/core/bms_afe_guard.c', 'bms/core/bms_state.c'],
      ['-Wno-misleading-indentation'], name='afe-health-guard')
run_c(guard, ['bms/core/bms_afe_guard.c', 'bms/core/bms_state.c'],
      ['-Wno-misleading-indentation', '-DBMS_AFE_REGISTER_DEBUG_ENABLE=1'], name='afe-health-debug-guard')

if os.environ.get('BMS_PRODUCT') in ('d011', 'd013', 'd014'):
    from d014_safety_loop_host_check import SOURCES
    fixture = read('tests/fixtures/d014_safety_loop/loop.c').split('int main(void)')[0]
    fixture = fixture.replace('assert(p != GPIO_PB4 && p != GPIO_PB5);', 'assert(p != GPIO_PB5 || v == 0u);')
    fixture = fixture.replace('static int bus_failed, flash_cut = -1;',
                              'static int bus_failed, fail_pack_read, flash_cut = -1;')
    fixture = fixture.replace('assert(pos < sizeof(frame)); frame[pos] = tx;',
        'if(fail_pack_read && pos==1 && frame[0]==2 && tx==0x93)return SH3673520_PORT_ERR_SPI;\n'
        '    assert(pos < sizeof(frame)); frame[pos] = tx;')
    fixture += r'''
int main(void){
 memset(flash,255,sizeof(flash));
 for(unsigned i=0;i<20;i++)raw16((uint8_t)(0x69+2*i),21120);
 for(unsigned i=0;i<4;i++)raw16((uint8_t)(0x5d+2*i),16384);
 raw16(0x93,(uint16_t)(SH3673510_BOARD_CELL_COUNT*3300u*32u/125u));bms_parameters_init();bms_afe_init();request_outputs();steps(12);
 assert(charge_on()&&discharge_on());
 /* 状态 RC 已成功但包压事务失败：保留事件时间，下一轮补读不推进恢复新样本。 */
 fail_pack_read=1;step();assert(charge_on()&&discharge_on());
 uint32_t observed=bms_time_ms();bms_afe_aux_measurements_t m;
 assert(!bms_afe_get_aux_measurements(&m));
 fail_pack_read=0;ready_bits=0;step();
 assert(bms_afe_get_aux_measurements(&m)&&m.sample_time_ms>observed);
 ready_bits=3;step();
 for(unsigned i=0;i<12;i++){
  bus_failed=1;step();assert(charge_on()&&discharge_on()&&!bms_error_get(BMS_ERROR_AFE1));
  bus_failed=0;step();assert(charge_on()&&discharge_on());
 }
 bus_failed=1;steps(5);assert(charge_on()&&discharge_on()&&!bms_error_get(BMS_ERROR_AFE1));
 step();assert(bms_error_get(BMS_ERROR_AFE1)&&!bms_afe_bus_access_allowed());
 bus_failed=0;steps(188);assert(charge_on()&&discharge_on());
 ready_bits=0;steps(100);assert(charge_on()&&discharge_on()&&!bms_error_get(BMS_ERROR_AFE1));
 ready_bits=3;steps(5);assert(charge_on()&&discharge_on());
 /* 已验证 E2P 错误优先于测量和容错。 */
 regs[SH3673520_REG_BSTATUS1]|=SH3673520_BSTATUS1_E2P_ERR_MASK;
 step();require_both_off();assert(bms_error_get(BMS_ERROR_AFE1));
 regs[SH3673520_REG_BSTATUS1]&=(uint8_t)~SH3673520_BSTATUS1_E2P_ERR_MASK;
 steps(5);assert(charge_on()&&discharge_on());
 bms_protection_params_t p=g_bms_protection_params;
 p.cell_ovp_first_mv=3550;p.cell_ovp_second_mv=3650;p.cell_ovp_third_mv=3750;
 p.cell_ovp_recover_mv=3500;p.cell_ovp_filter_10ms=40;
 assert(bms_protection_params_commit(&p));
 /* CADC 过期不能暂停仍有效的电压保护。 */
 ready_bits=SH3673520_FLAG2_VADC_MASK;steps(3);
 raw16(SH3673520_REG_CELL1H,26240);steps(3);
 assert(!charge_on()&&g_bms_report.fault_third.bits.cell_ovp&&!bms_error_get(BMS_ERROR_AFE1));
 raw16(SH3673520_REG_CELL1H,21120);ready_bits=3;steps(12);
 p=g_bms_protection_params;
 p.discharge_ocp_first_a10=100;p.discharge_ocp_second_a10=150;p.discharge_ocp_third_a10=200;
 p.discharge_ocp_recover_a10=50;p.discharge_ocp_filter_10ms=40;
 assert(bms_protection_params_commit(&p));
 /* VADC 过期不能暂停仍有效的电流保护，也不能解除旧电压故障。 */
 ready_bits=SH3673520_FLAG2_CADC_MASK;steps(3);
 raw16(SH3673520_REG_CADCDH,(uint16_t)-20000);steps(2);
 assert(!discharge_on()&&g_bms_report.fault_third.bits.discharge_ocp&&!bms_error_get(BMS_ERROR_AFE1));
 puts("PASS SH原始TU：间歇总线故障保持、持续1秒静默、无ADC事件持续通信20秒不关MOS、E2P立即阻断");return 0;
}
'''
    run_c(fixture, SOURCES, ['-I', str(ROOT/'tests/fixtures/d014_safety_loop/include'),
                            '-Wno-unused-function'], name='sh-health-chain')

evidence({'domain': 'afe_health', 'product': os.environ.get('BMS_PRODUCT'),
          'boundary': '真实guard/State与后端契约替身；SH另含原始采样/控制/CRC/SW/feature/存储TU，SPI/Flash/时间为替身，不代表物理Gate'})
