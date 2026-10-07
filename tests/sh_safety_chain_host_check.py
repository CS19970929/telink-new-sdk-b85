"""串接实际 SH 后端、guard、软件保护；保留芯片/feature 环境替身并明确边界。"""
import re
from project_paths import selected_source
from validation_support import ROOT, read, function, run_c, evidence

fixture=read('tests/fixtures/sh_recovery_evidence.c').split('#if defined(MODE_TEST)')[0]
for signature in ('uint8_t bms_sw_protection_charge_blocked(', 'uint8_t bms_sw_protection_discharge_blocked(',
                  'void bms_sw_protection_init(', 'void bms_sw_protection_update(',
                  'void bms_sw_protection_clear(', 'void bms_sw_protection_record_fault_edges(',
                  'void bms_sw_protection_reset_current_recovery(',
                  'uint8_t bms_sw_protection_discharge_overcurrent_active('):
    fixture=fixture.replace(function(fixture,signature),'')
profile=re.search(r'typedef struct\s*\{.*?\} bms_afe_hw_profile_t;',read('bms/core/bms_afe_hw_profile.h'),re.S).group()
fixture=fixture.replace('/* PROFILE TYPE */',profile)
fixture=fixture.replace('/* PRODUCTION */',selected_source(ROOT/'bms/afe/sh3673510/sh3673510_bms.c'))
fixture=fixture.replace('/* GUARD */',selected_source(ROOT/'bms/core/bms_afe_guard.c'))
fixture+='''
bms_protection_params_t g_bms_protection_params;
uint8_t bms_protection_params_valid(void){return 1;}
void bms_fault_history_record(bms_fault_code_t f){(void)f;}
int main(void){
 g_bms_protection_params.cell_ovp_third_mv=3750;g_bms_protection_params.cell_ovp_recover_mv=3500;g_bms_protection_params.cell_ovp_filter_10ms=40;
 g_bms_protection_params.cell_uvp_third_mv=3000;g_bms_protection_params.cell_uvp_recover_mv=3100;g_bms_protection_params.cell_uvp_filter_10ms=40;
 now=0xffff8000u;device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
 bms_afe_init();bms_afe_set_output_enabled(1);bms_afe_set_fets(1,1);
 for(unsigned i=0;i<10;i++)sample();
 assert(bms_afe_samples_qualified() && command_c && command_d);
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
 invalid_ntc=-1;bad_cell=-1;
 for(unsigned i=0;i<10;i++)sample();assert(command_c && command_d);
 device.flag2=SH3673520_FLAG2_VADC_MASK;
 sample();sample();sample();assert(!command_c && !command_d && !bms_afe_samples_qualified());
 puts("PASS 实际 SH 采样/guard/SW 保护/最终 FET 命令：OV、UV、并发 SC、NTC 无效、ADC 过期、tick 回绕");return 0;
}
'''
output=run_c(fixture,['bms/core/bms_sw_protection.c'],['-DGUARD_TEST=1','-Wno-misleading-indentation'],name='sh-safety-chain')
evidence({'domain':'safety_chain','observations':output.strip(),
          'boundary':'真实 SH backend+guard+SW；control/芯片、feature、存储为替身；命令不代表物理 Gate'})
