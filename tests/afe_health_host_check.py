"""D008 guard 与限时线路验证。"""
import os
from validation_support import read, run_c, function

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
static uint32_t now;
static unsigned c,d,writes,samples,initializations,sleep_calls;
static unsigned set_calls, fail_next_control;
static uint8_t recovery_off_ok;
static uint8_t control_ok=1, balance_ok=1, balance_get_ok=1, balance_used_known, direction_allow_mask;
static bms_afe_sample_status_t status=BMS_AFE_SAMPLE_OK;
static int32_t measured_current;
uint32_t bms_time_ms(void){return now;}
void bms_features_init(void){}
void bms_features_stop(void){}
void bms_sw_protection_init(void){}
void bms_sw_protection_reset_filters(void){}
uint8_t bms_protection_params_valid(void){return 1;}
void bms_afe_driver_request_voltage_check(void){}
void bms_afe_driver_reset_voltage_check(void){}
uint8_t bms_afe_driver_stop_voltage_check(void){return 1u;}
void bms_afe_driver_init(void){++initializations;}
uint8_t bms_afe_driver_prepare_recovery(void){if(!recovery_off_ok)return 0;c=d=0;return 1;}
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
 /* 关闭已验证时不等硬件WDT；静默后首次初始化失败也必须短间隔再试。 */
 qualify();recovery_off_ok=1;status=BMS_AFE_SAMPLE_READ_ERROR;poll();now+=1000u;poll();
 unsigned recoveries=initializations;now+=201u;poll();
 assert(initializations==recoveries+1u && !c && !d);
 status=BMS_AFE_SAMPLE_OK;poll();assert(c&&d&&!bms_error_get(BMS_ERROR_AFE1));
 recovery_off_ok=0;status=BMS_AFE_SAMPLE_ERROR;poll();poll();
 recoveries=initializations;now+=5001u;poll();assert(initializations==recoveries+1u);
 now+=200u;poll();now+=200u;poll();now+=201u;poll();
 assert(initializations==recoveries+2u && !c && !d);
 now+=200u;poll();now+=200u;poll();now+=201u;poll();
 assert(initializations==recoveries+3u);
 now+=200u;poll();now+=200u;poll();
 for(unsigned i=0;i<20u;i++){now+=201u;poll();}
 assert(initializations==recoveries+3u && !bms_afe_bus_access_allowed());
 now+=1000u;poll();
 status=BMS_AFE_SAMPLE_OK;poll();qualify();
 /* 明确关闭失败立即禁止输出，不用普通读错误的一秒确认。 */
 control_ok=0;assert(!bms_afe_set_channel_allowed(0,0));
 assert(bms_error_get(BMS_ERROR_AFE1));control_ok=1;qualify();
 /* 双禁或电流证据失效都会撤销临时放行。 */
 for(unsigned channel=0;channel<2;channel++){
  qualify();direction_allow_mask=(uint8_t)(1u<<channel);
  assert(bms_afe_set_channel_allowed((uint8_t)channel,0)&&c&&d);
  status=BMS_AFE_SAMPLE_READ_ERROR;poll();assert(c==(channel==1u)&&d==(channel==0u));
  assert(bms_afe_set_channel_allowed((uint8_t)(1u-channel),0));
  assert(!c&&!d);
  status=BMS_AFE_SAMPLE_OK;direction_allow_mask=(uint8_t)(1u<<channel);poll();assert(!c&&!d);
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
 assert(!bms_error_get(BMS_ERROR_AFE1)&&bms_afe_bus_access_allowed());
 poll();qualify();
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
 puts("PASS guard：999/1000ms、tick回绕、瞬时/间歇失败无OFF、只关闭不新开、事务持续失败、静默和完整有效读取恢复、配置禁止");
 return 0;
}
'''
run_c(guard, ['bms/core/bms_afe_guard.c', 'bms/core/bms_state.c'],
      ['-Wno-misleading-indentation'], name='afe-health-guard')
source = read('bms/afe/dvc1124/dvc1124_bms.c')
end = source.index('} s_voltage_check;') + len('} s_voltage_check;')
start = source.index('/* 校准电压检查：阶段只由本后端推进')
production = source[start:end] + '\n' + '\n'.join(function(source, sig) for sig in (
    'void bms_afe_driver_reset_voltage_check(', 'void bms_afe_driver_request_voltage_check(',
    'uint8_t bms_afe_driver_stop_voltage_check(', 'static uint8_t dvc_voltage_check_service('))
fixture = read('tests/fixtures/soc_voltage_check.c').replace('/* PRODUCTION */', production)
run_c(fixture, name='dvc-line-check')
