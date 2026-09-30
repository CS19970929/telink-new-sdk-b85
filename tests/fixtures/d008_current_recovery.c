#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "dvc1124.h"
#include "bms_diag.h"
static struct { struct { struct { uint8_t b1IchgOcp, b1IdischgOcp; } bits; } unMdlFault_Third; } g_stCellInfoReport;
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
static int32_t current_ma;
static uint8_t read_ok=1, write_ok=1, read_alarm, writes, last_clear;
static dvc1124_fet_drive_t chg_mode, dsg_mode;
uint8_t DVC1124_ClearAlarmFlags(uint8_t mask) { ++writes; last_clear=mask; return write_ok; }
uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *value, uint8_t count) {
    (void)reg; (void)count; *value=read_alarm; return read_ok;
}
void bms_diag_trace(uint16_t event, uint32_t a, uint32_t b) { (void)event; (void)a; (void)b; }
static uint8_t dvc_charge_blocked(void) { return g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp; }
static uint8_t dvc_discharge_blocked(void) { return g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp; }
static uint8_t dvc_set_fet_modes_if_changed(dvc1124_fet_drive_t c,dvc1124_fet_drive_t d) { chg_mode=c; dsg_mode=d; return 1; }
/* PRODUCTION */
static uint8_t step(uint32_t tick,uint8_t alarm,uint8_t removed,uint8_t driver_on,uint8_t swc,uint8_t swd) {
    dvc1124_snapshot_t snap={0}; snap.valid=1; snap.current_ma=current_ma; snap.sample_tick_32k=tick; snap.status=driver_on ? DVC1124_CC2_DSGF_MASK : 0;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp=swc;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp=swd;
    return dvc_recover_current_faults(&snap,alarm,removed);
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
    step(0xffff0000u+DVC_OCC_RECOVERY_TICKS,0,0,0,0,0); assert(!s_current_recovery.charge); /* wrap */
    reset(); step(0,0,0,0,1,1); step(DVC_OCC_RECOVERY_TICKS,0,0,0,0,0);
    assert(!s_current_recovery.charge && s_current_recovery.discharge); /* independent timers */
#if DVC1124_HW_PROTECT_ENABLE
    reset(); step(0,DVC_DSG_ALARMS,0,0,0,0); assert(s_current_recovery.discharge);
    step(32000,0,0,0,0,0); assert(s_current_recovery.discharge); /* AFE reinit cleared flags */
    step(40000,0,1,0,0,0); read_ok=0; step(46400,0,1,0,0,0); assert(s_current_recovery.discharge);
    read_ok=1; write_ok=0; step(50000,0,1,0,0,0); assert(s_current_recovery.discharge);
    write_ok=1; read_alarm=DVC1124_ALARM_SCD_MASK; step(60000,0,1,0,0,0); assert(s_current_recovery.discharge);
    read_alarm=0; step(70000,0,1,0,0,0); assert(!s_current_recovery.discharge && !s_current_recovery.hw_pending);
    assert(last_clear==DVC1124_ALARM_SCD_MASK);
    reset(); step(0,DVC_OCC_ALARMS,0,0,0,0); step(DVC_OCC_RECOVERY_TICKS-1, DVC_OCC_ALARMS,0,0,0,0); assert(!writes);
    step(DVC_OCC_RECOVERY_TICKS,DVC_OCC_ALARMS,0,0,0,0); assert(!s_current_recovery.charge && writes==1);
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
    return 0;
}
