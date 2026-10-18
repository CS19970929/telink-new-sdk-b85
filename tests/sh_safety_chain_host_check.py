"""串接实际 SH 后端、guard、软件保护；保留芯片/feature 环境替身并明确边界。"""
import re
from project_paths import selected_source
from validation_support import ROOT, read, function, run_c, evidence

fixture=read('tests/fixtures/sh_recovery_evidence.c').split('#if defined(MODE_TEST)')[0]
for signature in ('uint8_t bms_sw_protection_charge_blocked(', 'uint8_t bms_sw_protection_discharge_blocked(',
                  'void bms_sw_protection_init(', 'void bms_sw_protection_update(',
                  'void bms_sw_protection_clear(', 'void bms_sw_protection_record_fault_edges(',
                  'void bms_sw_protection_reset_filters(',
                  'void bms_sw_protection_reset_current_recovery(',
                  'uint8_t bms_sw_protection_discharge_overcurrent_active('):
    fixture=fixture.replace(function(fixture,signature),'')
fixture=fixture.replace('static bms_sw_protection_inputs_t last_sw_sample;', '')
fixture=fixture.replace('static int32_t bad_cell_mv;',
                        'static int32_t bad_cell_mv;\nstatic int32_t mos_ntc_ohm=10000;')
fixture=fixture.replace(function(fixture, 'uint16_t bms_lookup_u16('), r'''
uint16_t bms_lookup_u16(const uint16_t *p,uint16_t n,uint16_t x) {
 (void)p;(void)n;
 if(x==200u)return 1350u; /* 95 C */
 if(x==180u)return 1250u; /* 85 C */
 if(x==150u)return 1200u; /* 80 C */
 return 650u;
}
''')
fixture=fixture.replace('t[i]=(i==invalid_ntc)?0:10000;',
    't[i]=(i==invalid_ntc)?0:((i==SH3673510_BOARD_MOS_NTC_INDEX)?mos_ntc_ohm:10000);')
profile=re.search(r'typedef struct\s*\{.*?\} bms_afe_hw_profile_t;',read('bms/core/bms_afe_hw_profile.h'),re.S).group()
fixture=fixture.replace('/* PROFILE TYPE */',profile)
fixture=fixture.replace('/* PRODUCTION */',selected_source(ROOT/'bms/afe/sh3673510/sh3673510_bms.c'))
fixture=fixture.replace('/* GUARD */',selected_source(ROOT/'bms/core/bms_afe_guard.c'))
fixture+='''
bms_protection_params_t g_bms_protection_params;
void bms_fault_history_record(bms_fault_code_t f){(void)f;}
int main(void){
 g_bms_protection_params.cell_ovp_third_mv=3750;g_bms_protection_params.cell_ovp_recover_mv=3500;g_bms_protection_params.cell_ovp_filter_10ms=40;
 g_bms_protection_params.cell_uvp_third_mv=3000;g_bms_protection_params.cell_uvp_recover_mv=3100;g_bms_protection_params.cell_uvp_filter_10ms=40;
 now=0xffff8000u;device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
 bms_afe_init();bms_afe_set_output_enabled(1);bms_afe_set_fets(1,1);
 for(unsigned i=0;i<10;i++)sample();
 assert(((bms_afe_get_guard_diagnostic_bits() & DIAG_GUARD_SAMPLES_QUALIFIED) != 0u) && command_c && command_d);
 /* 真实 SW/guard/backend：只有 CADC 的可靠反向电流可解除软件 OC。 */
 g_bms_protection_params.discharge_ocp_third_a10=20;
 g_bms_protection_params.discharge_ocp_recover_a10=5;
 g_bms_protection_params.discharge_ocp_filter_10ms=40;
 public_current_ma=10000;sample();sample();
 assert(!command_d && bms_sw_protection_discharge_overcurrent_active());
 public_current_ma=-201;device.flag2=SH3673520_FLAG2_VADC_MASK;
 sample();assert(!command_d && bms_sw_protection_discharge_overcurrent_active());
 device.flag2=SH3673520_FLAG2_CADC_MASK;sample();
 assert(command_d && !bms_sw_protection_discharge_overcurrent_active());
 assert(last_sample_result==BMS_AFE_SAMPLE_OK);
 device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
 g_bms_protection_params.charge_ocp_third_a10=20;
 g_bms_protection_params.charge_ocp_recover_a10=5;
 g_bms_protection_params.charge_ocp_filter_10ms=40;
 public_current_ma=-10000;sample();sample();assert(!command_c);
 public_current_ma=201;device.flag2=SH3673520_FLAG2_VADC_MASK;
 sample();assert(!command_c);
 device.flag2=SH3673520_FLAG2_CADC_MASK;sample();assert(command_c);
 assert(last_sample_result==BMS_AFE_SAMPLE_OK);
 device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
 public_current_ma=0;sample();

 /* MOS 95 C 动作，85 C 时经 guard watchdog 恢复仍须保持；80 C 重新确认。 */
 g_bms_protection_params.mos_otp_third_x10=1350;
 g_bms_protection_params.mos_otp_recover_x10=1200;
 g_bms_protection_params.mos_otp_filter_10ms=40;
 mos_ntc_ohm=20000;sample();sample();
 assert(!command_c && !command_d && g_bms_report.fault_third.bits.mos_otp);
 mos_ntc_ohm=18000;sample();
 fail_stage=1u;sample();sample();
 assert(!bms_afe_bus_access_allowed());
 fail_stage=0u;now+=(35000u+1u)*32u;sample();
 for(unsigned i=0;i<10;i++)sample();
 assert(!bms_error_get(BMS_ERROR_AFE1));
 assert(g_bms_report.fault_third.bits.mos_otp && !command_c && !command_d);
 mos_ntc_ohm=15000;sample();assert(g_bms_report.fault_third.bits.mos_otp);
 sample();assert(!g_bms_report.fault_third.bits.mos_otp && command_c && command_d);
 mos_ntc_ohm=10000;sample();

 /* 完整入口只给 VADC：清 OV，不可借同轮零电流清 OCD；CADC 反向充电后再解除。 */
 device.flag1=SH3673520_FLAG1_OV_MASK|SH3673520_FLAG1_OCD1_MASK;
 device.flag2=SH3673520_FLAG2_VADC_MASK;
 sample();
 assert(!(device.flag1&SH3673520_FLAG1_OV_MASK));
 assert(device.flag1&SH3673520_FLAG1_OCD1_MASK);
 sample();assert(!command_d);
 public_current_ma=-201;device.flag2=SH3673520_FLAG2_CADC_MASK;
 sample();assert(!(device.flag1&SH3673520_FLAG1_OCD1_MASK));
 sample();assert(command_c && command_d);
 public_current_ma=0;device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
 sample();

 bms_afe_feature_snapshot_t temperature;
 assert(bms_afe_get_feature_snapshot(&temperature) && temperature.battery_temp_valid);
 bad_cell=0;bad_cell_mv=3750;
 sample();assert(command_c && command_d);
 sample();assert(!command_c && command_d && bms_sw_protection_charge_blocked());
 device.flag1=SH3673520_FLAG1_SC_MASK;sample();assert(!command_c && !command_d);
 /* 硬件 SC 尚在，软件 OV 消失不能误开另一方向。 */
 bad_cell=-1;sample();sample();assert(!bms_sw_protection_charge_blocked());assert(!command_d);
 /* AFE 重初始化不能清除短路锁存；随后注入已观测到的 LOADOFF。 */
 device.flag1=0;bms_afe_init();bms_afe_set_output_enabled(1);bms_afe_set_fets(1,1);
 for(unsigned i=0;i<10;i++)sample();assert(!command_d);
 device.bstatus2=SH3673520_BSTATUS2_LOADOFF_MASK;
 for(unsigned i=0;i<12;i++)sample();assert(command_c && command_d);
 bad_cell=0;bad_cell_mv=3000;sample();sample();assert(command_c && !command_d);
 invalid_ntc=SH3673510_BOARD_BAT_NTC1_INDEX;sample();assert(!command_c && !command_d);
 assert(bms_afe_get_feature_snapshot(&temperature) && !temperature.battery_temp_valid);
 invalid_ntc=-1;bad_cell=-1;
 for(unsigned i=0;i<10;i++)sample();assert(command_c && command_d);
 device.flag2=0;
 for(unsigned i=0;i<100;i++){sample();assert(command_c && command_d);}
 assert(bms_afe_get_guard_diagnostic_bits() & DIAG_GUARD_SAMPLES_QUALIFIED);
 assert(bms_afe_get_feature_snapshot(&temperature) && temperature.battery_temp_valid);
 puts("PASS 实际 SH 采样/guard/SW 保护/最终 FET 命令：OV、UV、并发 SC、NTC 无效、无ADC事件持续通信、tick 回绕");return 0;
}
'''
output=run_c(fixture,['bms/core/bms_sw_protection.c'],
             ['-DGUARD_TEST=1','-DBMS_SERVICE_DIAG_ENABLE=1','-Wno-misleading-indentation'],
             name='sh-safety-chain')
evidence({'domain':'safety_chain','observations':output.strip(),
          'boundary':'真实 SH backend+guard+SW；control/芯片、feature、存储为替身；命令不代表物理 Gate'})
