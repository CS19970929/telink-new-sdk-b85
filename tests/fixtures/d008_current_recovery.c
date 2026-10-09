#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "dvc1124.h"
#include "bms_diag.h"
static struct { struct { struct { uint8_t charge_ocp, discharge_ocp; } bits; } fault_third; } g_bms_report;
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
static int32_t current_ma;
static uint8_t read_ok=1, write_ok=1, read_alarm, writes, last_clear;
static dvc1124_fet_drive_t chg_mode, dsg_mode;
uint8_t DVC1124_ClearAlarmFlags(uint8_t mask) { ++writes; last_clear=mask; return write_ok; }
uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *value, uint8_t count) {
    (void)reg; (void)count; *value=read_alarm; return read_ok;
}
void bms_diag_trace(uint16_t event, uint32_t a, uint32_t b) { (void)event; (void)a; (void)b; }
static uint8_t dvc_charge_blocked(void) { return g_bms_report.fault_third.bits.charge_ocp; }
static uint8_t dvc_discharge_blocked(void) { return g_bms_report.fault_third.bits.discharge_ocp; }
static uint8_t dvc_set_fet_modes_if_changed(dvc1124_fet_drive_t c,dvc1124_fet_drive_t d) { chg_mode=c; dsg_mode=d; return 1; }
/* PRODUCTION */
static uint8_t step(uint32_t tick,uint8_t alarm,uint8_t removed,uint8_t driver_on,uint8_t swc,uint8_t swd) {
    dvc1124_snapshot_t snap={0}; snap.valid=1; snap.current_fresh=1; snap.current_ma=current_ma; snap.sample_tick_32k=tick; snap.status=6; snap.fet_status=driver_on ? DVC1124_CC2_DSGF_MASK : 0;
    g_bms_report.fault_third.bits.charge_ocp=swc;
    g_bms_report.fault_third.bits.discharge_ocp=swd;
    dvc_observe_current_recovery(&snap,removed,tick);
    return dvc_recover_current_faults(&snap,alarm,removed,tick);
}
static void reset(void) { memset(&s_current_recovery,0,sizeof(s_current_recovery)); current_ma=0; writes=0; write_ok=read_ok=1; read_alarm=0; }
int main(void) {
    reset(); step(100,0,1,1,0,1); assert(s_current_recovery.discharge);
    dvc_apply_common_port_fet_state(1,1); assert(chg_mode==DVC1124_FET_DRIVE_ON && dsg_mode==DVC1124_FET_DRIVE_AUTO_DIODE);
    step(99999,0,1,1,0,0); assert(s_current_recovery.discharge); /* GPIO high while ON ignored */
    step(100000,0,0,0,0,0); step(200000,0,0,0,0,0); assert(s_current_recovery.discharge); assert(bms_afe_current_recovery_pending()); /* zero current no release */
    step(210000,0,1,0,0,0); step(216399,0,1,0,0,0); assert(s_current_recovery.discharge);
    step(216400,0,0,0,0,0); step(220000,0,1,0,0,0); assert(s_current_recovery.discharge);
    step(226400,0,1,0,0,0); assert(!s_current_recovery.discharge);
    step(230000,0,0,0,0,1); assert(s_current_recovery.discharge); /* retrip */
    reset(); step(0xffff0000u,0,0,0,1,0);
    step(0xffff0000u+DVC_OCC_RECOVERY_TICKS-1u,0,0,0,0,0); assert(s_current_recovery.charge);
    step(0xffff0000u+DVC_OCC_RECOVERY_TICKS,0,0,0,0,0); assert(s_current_recovery.charge); /* zero current cannot prove release */
    current_ma=201;step(0xffff0000u+DVC_OCC_RECOVERY_TICKS+6400u,0,0,0,0,0);
    step(0xffff0000u+DVC_OCC_RECOVERY_TICKS+12800u,0,0,0,0,0);assert(!s_current_recovery.charge); /* stable reverse evidence across wrap */
    reset(); step(0,0,0,0,1,1); step(DVC_OCC_RECOVERY_TICKS,0,0,0,0,0);
    assert(s_current_recovery.charge && s_current_recovery.discharge);
    current_ma=201;step(DVC_OCC_RECOVERY_TICKS+6400u,0,0,0,0,0);step(DVC_OCC_RECOVERY_TICKS+12800u,0,0,0,0,0);
    assert(!s_current_recovery.charge && s_current_recovery.discharge); /* independent evidence */
#if DVC1124_HW_PROTECT_ENABLE
    reset(); step(0,DVC_DSG_ALARMS,0,0,0,0); assert(s_current_recovery.discharge);
    step(32000,0,0,0,0,0); assert(s_current_recovery.discharge); /* AFE reinit cleared flags */
    step(40000,0,1,0,0,0); read_ok=0; step(46400,0,1,0,0,0); assert(s_current_recovery.discharge);
    read_ok=1; write_ok=0; step(50000,0,1,0,0,0); assert(s_current_recovery.discharge);
    write_ok=1; read_alarm=DVC1124_ALARM_SCD_MASK; step(60000,0,1,0,0,0); assert(s_current_recovery.discharge);
    read_alarm=0; step(70000,0,1,0,0,0); assert(!s_current_recovery.discharge && !s_current_recovery.hw_pending);
    assert(last_clear==DVC1124_ALARM_SCD_MASK);
    reset(); step(0,DVC_OCC_ALARMS,0,0,0,0); step(DVC_OCC_RECOVERY_TICKS-1, DVC_OCC_ALARMS,0,0,0,0); assert(!writes);
    step(DVC_OCC_RECOVERY_TICKS,DVC_OCC_ALARMS,0,0,0,0); assert(s_current_recovery.charge && !writes);
    current_ma=201;step(DVC_OCC_RECOVERY_TICKS+6400u,DVC_OCC_ALARMS,0,0,0,0);
    step(DVC_OCC_RECOVERY_TICKS+12800u,DVC_OCC_ALARMS,0,0,0,0); assert(!s_current_recovery.charge && writes==1);
#endif
    reset(); step(0,0,0,0,0,1);
    current_ma=-200; step(10000,0,0,1,0,0); step(20000,0,0,1,0,0); assert(s_current_recovery.discharge);
    current_ma=-201; step(30000,0,0,1,0,0); step(36400,0,0,1,0,0); assert(!s_current_recovery.discharge);
    reset(); step(0,0,1,0,0,1); /* switching recovery evidence restarts qualification */
    current_ma=-1000; step(6400,0,0,1,0,0); assert(s_current_recovery.discharge);
    step(12800,0,0,1,0,0); assert(!s_current_recovery.discharge);
    reset(); step(0,0,1,0,0,1);
    step(32000,0,1,0,0,0); assert(s_current_recovery.discharge); /* acquisition gap */
    step(38400,0,1,0,0,0); assert(!s_current_recovery.discharge);
    reset(); step(0,0,0,0,0,1);
    /* Reproduce BLE-only 800 ms cadence: qualification keeps restarting. */
    for(uint32_t t=25600;t<=102400;t+=25600) step(t,0,1,0,0,0);
    assert(bms_afe_current_recovery_pending());
    /* A recovery-only 200 ms deadline completes qualification without BLE. */
    step(108800,0,1,0,0,0); assert(!bms_afe_current_recovery_pending());
    /* 200ms 每轮观察，但新电流间隔略超400ms：不丢恢复窗口。 */
    reset(); step(100,0,0,0,0,1);
    current_ma=-3000;step(6500,0,0,1,0,0);
    dvc1124_snapshot_t cache={0};cache.valid=1;cache.current_ma=current_ma;cache.fet_status=DVC1124_CC2_DSGF_MASK;
    dvc_observe_current_recovery(&cache,0,12910);
    assert(s_current_recovery.removed_pending==2u && s_current_recovery.discharge);
    g_bms_report.fault_third.bits.discharge_ocp=0;
    assert(dvc_recover_current_faults(&cache,0,0,12910)==0);
    assert(s_current_recovery.discharge && g_bms_report.fault_third.bits.discharge_ocp); /* 缓存不能解除，必须保留仲裁阻断 */
    dvc_apply_common_port_fet_state(1,1);
    assert(chg_mode==DVC1124_FET_DRIVE_ON && dsg_mode==DVC1124_FET_DRIVE_AUTO_DIODE);
    step(19320,0,0,1,0,0);assert(!s_current_recovery.discharge);

    /* 等待转换时也必须撤销已经丢失的负载移除证据。 */
    reset();step(0,0,1,0,0,1);assert(s_current_recovery.removed_pending==1u);
    cache.current_ma=0;cache.fet_status=0;
    dvc_observe_current_recovery(&cache,0,6400);
    assert(!s_current_recovery.removed_pending && s_current_recovery.discharge);
    step(12800,0,1,0,0,0);assert(s_current_recovery.discharge);
    step(19200,0,1,0,0,0);assert(!s_current_recovery.discharge);

    /* 充电故障在等待转换期间也必须持续阻断，硬件待清标志不能丢失。 */
    reset();step(0,0,0,0,1,0);
    g_bms_report.fault_third.bits.charge_ocp=0;
    cache.current_ma=0;
    assert(dvc_recover_current_faults(&cache,0,0,6400)==0);
    assert(s_current_recovery.charge && g_bms_report.fault_third.bits.charge_ocp);
    dvc_apply_common_port_fet_state(1,1);
    assert(chg_mode==DVC1124_FET_DRIVE_AUTO_DIODE && dsg_mode==DVC1124_FET_DRIVE_ON);
#if DVC1124_HW_PROTECT_ENABLE
    reset();step(0,DVC_DSG_ALARMS,0,0,0,0);
    g_bms_report.fault_third.bits.discharge_ocp=0;
    assert(dvc_recover_current_faults(&cache,0,0,6400)==DVC_DSG_ALARMS);
    assert(g_bms_report.fault_third.bits.discharge_ocp && !writes);
#endif
    return 0;
}
