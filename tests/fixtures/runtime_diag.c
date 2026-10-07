/* 生产 Telink runtime TU：只替换诊断接收端，检查本帧参数与后端调用资格。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_diag.h"
#include "bms_afe.h"
#include "bms_parameters.h"
#include "bms_soc.h"
#include "bms_state.h"
#include "bms_afe_backend.h"
bms_report_t g_bms_report;
static unsigned samples,soc_reads,faults,aux_reads,storage,parameters,afe;
static uint8_t expected_valid;
static int32_t expected_raw,expected_current;
static uint32_t expected_tick;
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m) { (void)m; ++aux_reads;return 0; }
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
uint8_t bms_afe_current_recovery_pending(void) { return 1u; }
#endif
void bms_soc_get_diag(bms_soc_diag_t *soc) { memset(soc,0,sizeof(*soc));soc->soc_display=47;++soc_reads; }
void bms_diag_runtime_sample(uint8_t valid,int32_t raw,int32_t current,uint32_t tick,uint8_t recovery) {
    assert(valid==expected_valid && raw==expected_raw && current==expected_current && tick==expected_tick);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    assert(recovery==1u);
#else
    assert(recovery==0u);
#endif
    ++samples;
}
void bms_diag_runtime_soc(const bms_soc_diag_t *soc) { assert(soc->soc_display==47); }
void bms_diag_runtime_faults(uint16_t first,uint16_t second,uint16_t third) { assert(first==1 && second==2 && third==4);++faults; }
void bms_storage_platform_diag_poll(void) { ++storage; }
void bms_parameters_diag_poll(void) { ++parameters; }
void bms_afe_diag_poll(void) { ++afe; }
int main(void) {
    g_bms_report.unMdlFault_First.all=1;g_bms_report.unMdlFault_Second.all=2;g_bms_report.unMdlFault_Third.all=4;
    expected_valid=1;expected_raw=-123;expected_current=-120;expected_tick=UINT32_MAX-10u;
    bms_diag_poll_runtime(expected_valid,expected_raw,expected_current,expected_tick);
    expected_valid=0;expected_raw=expected_current=0;expected_tick=5;
    bms_diag_poll_runtime(expected_valid,expected_raw,expected_current,expected_tick);
    assert(samples==2 && soc_reads==2 && faults==2 && aux_reads==0);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    assert(storage==0 && parameters==0 && afe==0);
#else
    assert(storage==2 && parameters==2 && afe==2);
#endif
    puts("PASS production runtime diag: same-frame raw/current/tick, invalid zeros, no second aux read, original backend poll cadence");
    return 0;
}
