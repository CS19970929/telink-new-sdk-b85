"""完整选中 SH TU：九组恢复计数、同轮证据、清除次序和失败。"""
import re
from project_paths import selected_source
from validation_support import ROOT, read, function, run_c

fixture = read('tests/fixtures/sh_recovery_evidence.c').split('#if defined(MODE_TEST)')[0]
fixture = fixture.replace(function(fixture, 'static void sample('), '')
fixture = fixture.replace('static unsigned clear_calls;', '')
profile = re.search(r'typedef struct\s*\{.*?\} bms_afe_hw_profile_t;',
                    read('bms/core/bms_afe_hw_profile.h'), re.S).group()
fixture = fixture.replace('/* PROFILE TYPE */', profile + r'''
static bms_afe_hw_profile_t requested;
static sh3673510_protection_actual_t actual;
static uint8_t requested_ok=1u,actual_ok=1u,cleared1,cleared2;
static unsigned flag1_calls,flag2_calls;
''')
for signature, replacement in (
    ('uint8_t sh3673510_control_clear_flag1(',
     'uint8_t sh3673510_control_clear_flag1(uint8_t m){++flag1_calls;cleared1=m;return clear_ok!=1u;}'),
    ('uint8_t sh3673510_control_clear_flag2(',
     'uint8_t sh3673510_control_clear_flag2(uint8_t m){++flag2_calls;cleared2=m;return clear_ok!=2u;}'),
    ('uint8_t sh3673510_control_get_protection_actual(',
     'uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *p){*p=actual;return actual_ok;}'),
    ('uint8_t bms_afe_hw_profile_get(',
     'uint8_t bms_afe_hw_profile_get(bms_afe_hw_profile_t *p){*p=requested;return requested_ok;}'),
):
    fixture = fixture.replace(function(fixture, signature), replacement)
fixture = fixture.replace('/* PRODUCTION */', selected_source(ROOT/'bms/afe/sh3673510/sh3673510_bms.c'))
fixture = fixture.replace('/* GUARD */', selected_source(ROOT/'bms/core/bms_afe_guard.c'))
fixture += r'''
static bms_sw_protection_inputs_t evidence(void) {
    bms_sw_protection_inputs_t v;memset(&v,0,sizeof(v));
    v.discharge_recovery_immediate=sh3510_fresh_charging();
    v.charge_recovery_immediate=sh3510_fresh_discharging();
    v.discharge_recovery_allowed=s_load_removed||v.discharge_recovery_immediate;
    v.battery_temp_valid=battery_temperature_snapshot(&v.battery_temp_min,&v.battery_temp_max);
    return v;
}
static void trace_reset(void) { flag1_calls=flag2_calls=0u;cleared1=cleared2=0u; }
int main(void) {
    static const uint8_t masks[]={SH3673520_FLAG1_OV_MASK,SH3673520_FLAG1_UV_MASK,
        SH3673520_FLAG1_OCD1_MASK,SH3673520_FLAG1_OCD2_MASK,SH3673520_FLAG1_OCC_MASK,
        SH3673520_FLAG2_OTC_MASK,SH3673520_FLAG2_OTD_MASK,SH3673520_FLAG2_UTC_MASK,SH3673520_FLAG2_UTD_MASK};
    static const u16 needed[]={2,3,5,5,4,6,6,6,6};
    sh3673510_control_status_t status;memset(&status,0,sizeof(status));
    assert(HW_REC_COUNT==9u);
    requested.cov_recover_mv=4000u;requested.cuv_recover_mv=2800u;
    requested.ocd_recover_a10=requested.occ_recover_a10=10u;
    requested.cov_recover_ms=400u;requested.cuv_recover_ms=600u;
    requested.ocd_recover_ms=1000u;requested.occ_recover_ms=800u;requested.temp_recover_ms=1200u;
    requested.chg_ot_recover_x10=requested.dsg_ot_recover_x10=700u;
    requested.chg_ut_recover_x10=requested.dsg_ut_recover_x10=600u;
    actual.ov_mv=4200u;actual.uv_mv=2500u;actual.ocd1_a10=actual.ocd2_a10=actual.occ_a10=100u;
    g_bms_report.cell_min_mv=g_bms_report.cell_max_mv=3300u;
    g_bms_report.temperature_x10[AFE1_TEMP1]=640u;g_bms_report.temperature_x10[AFE1_TEMP2]=660u;
    s_ntc_ohm[SH3673510_BOARD_BAT_NTC1_INDEX]=s_ntc_ohm[SH3673510_BOARD_BAT_NTC2_INDEX]=10000u;
    s_confirm_sample=1u;s_aux.current_ma=0;s_load_removed=s_charger_removed=1u;clear_ok=0u;
    for(unsigned id=0;id<9u;++id)if(id<5u)status.flag1|=masks[id];else status.flag2|=masks[id];
    memset(s_hw_recovery_count,0,sizeof(s_hw_recovery_count));
    for(unsigned n=1;n<=6u;++n) {
        uint8_t c1=0,c2=0;bms_sw_protection_inputs_t v=evidence();trace_reset();
        assert(service_hw_flag_recovery(&status,&v));
        for(unsigned id=0;id<9u;++id) {
            assert(s_hw_recovery_count[id]==(n<needed[id]?n:needed[id]));
            if(n>=needed[id]){if(id<5u)c1|=masks[id];else c2|=masks[id];}
        }
        assert(cleared1==c1&&cleared2==c2);
    }
    /* 一个方向危险只撤销自己的两个计数，温度和另一方向保持独立。 */
    g_bms_report.discharge_current_a10=101u;
    bms_sw_protection_inputs_t v=evidence();assert(service_hw_flag_recovery(&status,&v));
    assert(!s_hw_recovery_count[HW_REC_OCD1]&&!s_hw_recovery_count[HW_REC_OCD2]);
    assert(s_hw_recovery_count[HW_REC_OCC]==4u&&s_hw_recovery_count[HW_REC_OTC]==6u);
    s_ntc_ohm[SH3673510_BOARD_BAT_NTC1_INDEX]=0u;v=evidence();trace_reset();
    assert(service_hw_flag_recovery(&status,&v));assert(!cleared2);
    for(unsigned id=5u;id<9u;++id)assert(!s_hw_recovery_count[id]);
    /* 等待转换不能把缓存反向电流当作即时恢复；201mA 新样本才可。 */
    status.flag1=SH3673520_FLAG1_OCD1_MASK|SH3673520_FLAG1_OCD2_MASK;status.flag2=0u;
    s_load_removed=0u;s_aux.current_ma=-201;s_confirm_sample=0u;
    v=evidence();trace_reset();assert(service_hw_flag_recovery(&status,&v));assert(!flag1_calls);
    s_confirm_sample=1u;v=evidence();trace_reset();assert(service_hw_flag_recovery(&status,&v));assert(cleared1==status.flag1);
    s_aux.current_ma=-200;v=evidence();trace_reset();assert(service_hw_flag_recovery(&status,&v));assert(!flag1_calls);
    /* FLAG1 失败时不得继续 FLAG2；FLAG2 失败同样向上传递。 */
    status.flag1=SH3673520_FLAG1_WK_MASK;status.flag2=SH3673520_FLAG2_WDT_MASK;
    for(unsigned fail=1;fail<=2u;++fail) {
        clear_ok=(uint8_t)fail;s_flag_clear_failed=0u;trace_reset();
        assert(!service_hw_flag_recovery(&status,&v));assert(s_flag_clear_failed);
        assert(flag1_calls==1u&&flag2_calls==(fail==2u));
    }
    requested_ok=0u;trace_reset();assert(!service_hw_flag_recovery(&status,&v));assert(!flag1_calls&&!flag2_calls);
    puts("PASS SH: nine independent timing windows, temperature validity, fresh direction/deadband, ordered W0C failures");
    return 0;
}
'''
run_c(fixture, flags=['-DBMS_SERVICE_DIAG_ENABLE=1'], name='sh-recovery-counters')
