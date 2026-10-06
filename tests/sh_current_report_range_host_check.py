"""实际SH测量发布：旧0.1A无符号协议饱和，极值不得回绕为零。"""
import re
from project_paths import selected_source
from validation_support import ROOT, read, run_c, evidence
fixture=read('tests/fixtures/sh_recovery_evidence.c').split('#if defined(MODE_TEST)')[0]
profile=re.search(r'typedef struct\s*\{.*?\} bms_afe_hw_profile_t;',read('bms/core/bms_afe_hw_profile.h'),re.S).group()
fixture=fixture.replace('/* PROFILE TYPE */',profile)
fixture=fixture.replace('static int32_t bms_config_calibrate_current(int32_t ma) { return ma; }',
 'static int32_t calibration_result;\nstatic int32_t bms_config_calibrate_current(int32_t ma) { (void)ma;return calibration_result; }')
fixture=fixture.replace('/* PRODUCTION */',selected_source(ROOT/'bms/afe/sh3673510/sh3673510_bms.c'))
fixture+=r'''
int main(void) {
 device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
 sh3673510_bms_afe_init();for(unsigned i=0;i<12;++i)sample();
 const int32_t input[]={0,49,50,6553450,6553550,6553600,INT32_MAX,-49,-50,-6553450,-6553550,-6553600,INT32_MIN};
 const uint16_t expected[]={0,0,1,65535,65535,65535,65535,0,1,65535,65535,65535,65535};
 for(unsigned i=0;i<sizeof(input)/sizeof(input[0]);++i) {
  calibration_result=input[i];sample();
  assert(g_stCellInfoReport.u16IDischg==(input[i]>=0?expected[i]:0));
  assert(g_stCellInfoReport.u16Ichg==(input[i]<0?expected[i]:0));
 }
 puts("PASS 13个校准电流边界，0.1A饱和与INT32_MIN有符号极值");return 0;
}
'''
out=run_c(fixture,name='sh-current-report-range')
evidence({'domain':'current_report','trace':out.strip(),'boundary':'实际SH后端函数体；校准结果注入，校准算法由独立数学回归覆盖；非实板极限精度'})
