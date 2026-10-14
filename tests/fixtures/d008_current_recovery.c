#include <stdint.h>
#include <string.h>
#include <assert.h>
#define GPIO_PB1 1u /* host 输入编号；GPIO 电平由下面的夹具提供。 */
#include "dvc1124.h"
#include "bms_diag.h"
static struct { struct { struct { uint8_t charge_ocp, discharge_ocp; } bits; } fault_third; } g_bms_report;
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
static int32_t current_ma;
#ifndef BMS_D008_LOAD_RELEASE_CONFIRM_MS
#define BMS_D008_LOAD_RELEASE_CONFIRM_MS 200u
#define BMS_BOARD_LOAD_REMOVED_LEVEL 1u
#define BMS_BOARD_LOAD_DETECT_PIN 1u
#endif
static uint8_t pb1;
static int gpio_read(unsigned pin) { (void)pin; return pb1; }
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
static uint8_t step(uint32_t tick,uint8_t alarm,uint8_t driver_on,uint8_t swc,uint8_t swd) {
    dvc1124_snapshot_t snap={0}; snap.valid=1; snap.current_fresh=1; snap.current_ma=current_ma; snap.sample_tick_32k=tick; snap.status=6; snap.fet_status=driver_on ? DVC1124_CC2_DSGF_MASK : 0;
    g_bms_report.fault_third.bits.charge_ocp=swc;
    g_bms_report.fault_third.bits.discharge_ocp=swd;
    dvc_observe_current_recovery(&snap,tick);
    return dvc_recover_current_faults(&snap,alarm,tick);
}
static void reset(void) { memset(&s_current_recovery,0,sizeof(s_current_recovery)); current_ma=0; pb1=0; writes=0; write_ok=read_ok=1; read_alarm=0; }
int main(void) {
    /* 零流/关闭 MOS 不等于负载移除；只接受新鲜的可靠反向电流。 */
    reset(); step(100,0,1,0,1); assert(s_current_recovery.discharge);
    dvc_apply_common_port_fet_state(1,1);
    assert(chg_mode==DVC1124_FET_DRIVE_ON && dsg_mode==DVC1124_FET_DRIVE_AUTO_DIODE);
    step(6500,0,0,0,0);step(12900,0,0,0,0);assert(s_current_recovery.discharge);
    current_ma=-200;step(19300,0,0,0,0);assert(s_current_recovery.discharge);
    dvc1124_snapshot_t cache={0};cache.valid=1;cache.current_ma=-201;
    assert(dvc_recover_current_faults(&cache,0,19400)==0 && s_current_recovery.discharge);
    current_ma=-201;step(19500,0,0,0,0);assert(!s_current_recovery.discharge);
    /* OCC 不再等待30秒；两方向证据独立。 */
    reset();step(100,0,0,1,1);current_ma=201;step(200,0,0,0,0);
    assert(!s_current_recovery.charge && s_current_recovery.discharge);
    current_ma=-201;step(300,0,0,0,0);assert(!bms_afe_current_recovery_pending());
    /* PB1 高且输出确实关闭，持续200ms才允许移除恢复；支持tick回绕。 */
    reset();pb1=1;step(0xfffff000u,0,0,0,1);
    step(0xfffff000u+6399u,0,0,0,0);assert(s_current_recovery.discharge);
    step(0xfffff000u+6400u,0,0,0,0);assert(!s_current_recovery.discharge);
    reset();pb1=1;step(100,0,1,0,1);step(6500,0,1,0,0);
    assert(s_current_recovery.discharge && !s_current_recovery.load_release_pending);
    step(12900,0,0,0,0);pb1=0;step(16000,0,0,0,0);
    pb1=1;step(19300,0,0,0,0);step(25699,0,0,0,0);assert(s_current_recovery.discharge);
    step(25700,0,0,0,0);assert(!s_current_recovery.discharge);
    /* 通信失效和观察间断使 PB1 窗口作废，不能跨间断累计。 */
    reset();pb1=1;step(100,0,0,0,1);
    cache.valid=0;dvc_observe_current_recovery(&cache,6500);
    step(12900,0,0,0,0);assert(s_current_recovery.discharge);
    step(19300,0,0,0,0);assert(!s_current_recovery.discharge);
    reset();pb1=1;step(100,0,0,0,1);step(32100,0,0,0,0);assert(s_current_recovery.discharge);
    step(38500,0,0,0,0);assert(!s_current_recovery.discharge);
    /* 软件状态机仍未解除时，后端不得抢先清锁存。 */
    reset();current_ma=-201;step(100,0,0,0,1);assert(s_current_recovery.discharge);
    step(200,0,0,0,0);assert(!s_current_recovery.discharge);
#if DVC1124_HW_PROTECT_ENABLE
    /* 每个放电硬保护位均可反向充电恢复，也可 PB1 移除恢复。 */
    const uint8_t faults[]={DVC1124_ALARM_OCD1_MASK,DVC1124_ALARM_OCD2_MASK,DVC1124_ALARM_SCD_MASK};
    for(unsigned i=0;i<sizeof(faults);++i) {
        reset();step(100,faults[i],0,0,0);current_ma=-201;
        step(200,faults[i],0,0,0);assert(!s_current_recovery.discharge && last_clear==faults[i]);
        reset();pb1=1;step(100,faults[i],0,0,0);
        step(6500,faults[i],0,0,0);assert(!s_current_recovery.discharge && last_clear==faults[i]);
    }
    reset();step(100,DVC_OCC_ALARMS,0,0,0);current_ma=201;
    step(200,DVC_OCC_ALARMS,0,0,0);assert(!s_current_recovery.charge && writes==1);
    /* I/O失败、重新置位及清除期间新故障均保持对应锁存。 */
    reset();step(100,DVC_DSG_ALARMS,0,0,0);current_ma=-201;read_ok=0;
    step(200,0,0,0,0);assert(s_current_recovery.discharge);
    read_ok=1;write_ok=0;step(300,0,0,0,0);assert(s_current_recovery.discharge);
    write_ok=1;read_alarm=DVC1124_ALARM_SCD_MASK;step(400,0,0,0,0);assert(s_current_recovery.discharge);
    read_alarm=DVC_OCC_ALARMS;step(500,0,0,0,0);
    assert(!s_current_recovery.discharge && s_current_recovery.charge);
    current_ma=201;read_alarm=0;step(600,0,0,0,0);assert(!bms_afe_current_recovery_pending());
    /* AFE重配已清硬件位，但 RAM 待清位和缓存阶段的输出阻断仍保留。 */
    reset();step(100,DVC_DSG_ALARMS,0,0,0);
    cache.valid=1;cache.current_fresh=0;cache.current_ma=0;
    g_bms_report.fault_third.bits.discharge_ocp=0;
    assert(dvc_recover_current_faults(&cache,0,200)==DVC_DSG_ALARMS);
    assert(g_bms_report.fault_third.bits.discharge_ocp);
#endif
    return 0;
}
