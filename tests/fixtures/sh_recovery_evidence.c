#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "bms_state.h"
#include "bms_afe_driver.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "bms_error.h"
#include "bms_diag.h"
#include "bms_sh3673510_config.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673510_ntc.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
typedef uint16_t u16;
/* PROFILE TYPE */
const uint16_t sh3673510_ntc_10k[60] = {0};
struct stCell_Info g_stCellInfoReport;
/* Only the report bits used by the selected backend are platform-owned. */
volatile bms_system_status_t g_bms_system_status;
static uint8_t errors[BMS_ERROR_COUNT];
static uint32_t now;
static sh3673510_control_status_t device;
static unsigned clear_calls;
static uint8_t clear_ok = 1, command_c, command_d;
static uint16_t cplus_raw = 1024; /* 4 V, charger remains connected */
static uint8_t load_mode;
static uint8_t mode_ok = 1;
static unsigned read_stage, fail_stage;
static int bad_cell=-1, invalid_ntc=-1;
static int32_t bad_cell_mv;
static uint8_t read_ok(void) { return ++read_stage != fail_stage; }
#ifndef MODE_TEST
uint8_t sh3673510_control_set_load_detection(uint8_t on,uint8_t *changed) { if(!mode_ok)return 0; *changed=(load_mode!=on);load_mode=on;return 1; }
#else
static uint8_t s_control_ready=1,s_afe_sleeping,mode_register;
static unsigned mode_stage,mode_fail,mode_writes;
static uint8_t mismatch;
sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *v) {
    assert(reg==SH3673520_REG_SCONF3);*v=mode_register;
    if(++mode_stage==mode_fail || !mode_ok)return SH3673520_ERR_SPI;
    if(mismatch && mode_stage>1)*v^=SH3673520_SCONF3_CRLD_EN_MASK;
    return SH3673520_OK;
}
sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t v) {
    assert(reg==SH3673520_REG_SCONF3);++mode_writes;
    if(++mode_stage==mode_fail)return SH3673520_ERR_SPI;
    mode_register=v;return SH3673520_OK;
}
/* MODE CONTROL */
#endif
uint8_t bms_error_get(bms_error_id_t id) { return errors[id]; }
void bms_error_raise(bms_error_id_t id) { errors[id] = 1; }
void bms_error_clear(bms_error_id_t id) { errors[id] = 0; }
#ifndef GUARD_TEST
uint8_t bms_afe_samples_qualified(void) { return 1; }
#else
void bms_features_init(void) {}
void bms_features_service(void) {}
void bms_features_on_afe_invalid(void) {}
uint8_t bms_features_outputs_blocked(void) { return 0; }
uint8_t sh3673510_backend_set_balance_mask(uint32_t m) { (void)m;return 1; }
uint8_t sh3673510_backend_get_balance_mask(uint32_t *m) { *m=0;return 1; }
uint8_t sh3673510_backend_openwire_start(void) { return 1; }
bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *r) { (void)r;return BMS_AFE_DIAG_ERROR; }
#endif
uint8_t bms_sw_protection_charge_blocked(void) { return 0; }
uint8_t bms_sw_protection_discharge_blocked(void) { return 0; }
uint8_t bms_features_charge_direction_blocked(void) { return 0; }
void bms_sw_protection_init(void) {}
void bms_sw_protection_update(const bms_sw_protection_inputs_t *p) { (void)p; }
void bms_sw_protection_reset_current_recovery(void) {}
uint8_t bms_sw_protection_discharge_overcurrent_active(void) { return 0; }
void bms_sw_protection_clear(void) {}
void bms_sw_protection_record_fault_edges(void) {}
static int32_t bms_config_calibrate_current(int32_t ma) { return ma; }
static uint32_t pm_get_32k_tick(void) { return now; }
uint16_t bms_lookup_u16(const uint16_t *p, uint16_t n, uint16_t x) { (void)p; (void)n; (void)x; return 650; }
uint8_t sh3673510_control_init(void) { load_mode = 0; return 1; }
uint8_t sh3673510_control_ready(void) { return 1; }
uint8_t sh3673510_control_wake(void) { return read_ok(); }
uint8_t sh3673510_control_sleep(void) { return 1; }
uint8_t sh3673510_control_apply_protection(void) { return 1; }
uint8_t sh3673510_control_set_fets(uint8_t c, uint8_t d) { command_c=c; command_d=d; return 1; }
uint8_t sh3673510_control_set_balance(uint16_t mask) { (void)mask; return 1; }
void sh3673510_board_set_heater(uint8_t on) { (void)on; }
void sh3673510_board_force_heater_fuse_safe(void) {}
uint8_t sh3673510_control_read_status(sh3673510_control_status_t *s) { *s=device; return read_ok(); }
uint8_t sh3673510_control_clear_flag1(uint8_t mask) { ++clear_calls; if(clear_ok) device.flag1 &= (uint8_t)~mask; return clear_ok; }
uint8_t sh3673510_control_clear_flag2(uint8_t mask) { ++clear_calls; if(clear_ok) device.flag2 &= (uint8_t)~mask; return clear_ok; }
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *a) {
    memset(a,0,sizeof(*a)); a->valid=1; a->ov_mv=4200; a->uv_mv=2500;
    a->occ_a10=100; a->ocd1_a10=200; a->ocd2_a10=300; return 1;
}
uint8_t bms_afe_hw_profile_get(bms_afe_hw_profile_t *p) {
    memset(p,0,sizeof(*p)); p->cov_recover_mv=4000; p->cuv_recover_mv=2800;
    p->occ_recover_a10=10; p->ocd_recover_a10=10;
    p->occ_recover_ms=100; p->ocd_recover_ms=2000; return 1;
}
void SH3673520_GetCommStats(sh3673520_comm_stats_t *s) { memset(s,0,sizeof(*s)); }
sh3673520_status_t SH3673520_ReadCellVoltages(int32_t *v,uint8_t n) { for(uint8_t i=0;i<n;++i)v[i]=(i==bad_cell)?bad_cell_mv:3300; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_ReadPackVoltage(int32_t *v) { *v=3300*SH3673510_BOARD_CELL_COUNT; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_ReadCurrent(sh3673520_current_raw_t *v) { memset(v,0,sizeof(*v)); return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_CurrentRawToMilliAmp(int32_t raw,uint32_t shunt,int32_t *v) { (void)raw;(void)shunt;*v=0; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_ReadTemperatures(sh3673520_temperature_raw_t *t) { for(uint8_t i=0;i<4;++i)t->external_raw[i]=(i==invalid_ntc)?0:10000; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_NtcRawToOhm(int32_t raw,uint32_t *v) { *v=(uint32_t)raw; return raw?SH3673520_OK:SH3673520_ERR_RANGE; }
sh3673520_status_t SH3673520_ReadRegs(uint8_t reg,uint8_t *v,sh3673520_size_t n) { (void)reg;assert(n==2);v[0]=(uint8_t)(cplus_raw>>8);v[1]=(uint8_t)cplus_raw;return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }

/* PRODUCTION */
#ifdef GUARD_TEST
/* GUARD */
#endif

static void sample(void) {
    now += 6400;
    read_stage=0;
#ifdef GUARD_TEST
    bms_afe_sample();
#else
    sh3673510_bms_afe_sample();
    sh3673510_bms_afe_set_fets(1,1);
#endif
}
#if defined(MODE_TEST)
int main(void) {
    uint8_t changed;
    /* p16/p32：保留稳定位，不重发自清TRG；其当前值不决定配置是否改变。 */
    for(unsigned initial=0;initial<=255;++initial)for(unsigned load=0;load<=1;++load) {
        mode_register=(uint8_t)initial;mode_stage=mode_writes=0;
        uint8_t target=(uint8_t)((initial&~0x0du)|(load?0x08u:0x04u));
        unsigned needs_write=((initial&0x7eu)!=(target&0x7eu));
        assert(sh3673510_control_set_load_detection((uint8_t)load,&changed));
        assert(mode_register==(needs_write?target:initial) && changed==needs_write);
        assert(mode_writes==needs_write);
    }
    for(unsigned fail=1;fail<=3;++fail) {
        mode_register=0;mode_stage=0;mode_fail=fail;
        assert(!sh3673510_control_set_load_detection(1,&changed));
    }
    mode_fail=0;mode_stage=0;mismatch=1;
    assert(!sh3673510_control_set_load_detection(0,&changed));
    mismatch=0;s_afe_sleeping=1;mode_stage=0;
    assert(!sh3673510_control_set_load_detection(1,&changed) && mode_stage==0);
    s_afe_sleeping=0;s_control_ready=0;
    assert(!sh3673510_control_set_load_detection(1,&changed) && mode_stage==0);
    s_control_ready=1;
    assert(!sh3673510_control_set_load_detection(1,0) && mode_stage==0);
    /* Keep the full backend linked in this mode too. */
    mode_ok=0;sample();
    puts("Actual SCONF3 RMW preserves unrelated bits; every I/O/readback failure is returned");
    return 0;
}
#elif defined(ATOMIC_TEST)
int main(void) {
    bms_afe_aux_measurements_t aux;
    bms_afe_feature_snapshot_t snap;
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    sh3673510_bms_afe_init();
    for(unsigned i=0;i<8;++i)sample();
    assert(sh3673510_bms_afe_get_aux_measurements(&aux));
    struct stCell_Info before=g_stCellInfoReport;
    for(unsigned f=1;f<=8;++f) {
        /* Re-establish the public healthy state before each independent fault. */
        fail_stage=0;sample();sample();
        fail_stage=f;sample();
        assert(!sh3673510_bms_afe_get_aux_measurements(&aux));
        assert(!memcmp(&before,&g_stCellInfoReport,sizeof(before)));
        assert(!sh3673510_backend_get_feature_snapshot(&snap));
    }
    fail_stage=0;
    for(unsigned c=0;c<SH3673510_BOARD_CELL_COUNT;++c)for(unsigned v=0;v<2;++v) {
        bad_cell=(int)c;bad_cell_mv=v?65536:-1;sample();
        assert(!sh3673510_bms_afe_get_aux_measurements(&aux));
        assert(!memcmp(&before,&g_stCellInfoReport,sizeof(before)));
    }
    bad_cell=-1;sample();sample();
    unsigned reads=read_stage;
    assert(sh3673510_backend_get_feature_snapshot(&snap) && snap.valid && snap.battery_temp_valid);
    assert(read_stage==reads && snap.cell_count==SH3673510_BOARD_CELL_COUNT);
    for(unsigned i=SH3673510_BOARD_CELL_COUNT;i<32;++i)assert(g_stCellInfoReport.u16VCell[i]==61001);
    invalid_ntc=SH3673510_BOARD_BAT_NTC1_INDEX;sample();
    assert(sh3673510_backend_get_feature_snapshot(&snap) && !snap.battery_temp_valid);
    device.flag1=SH3673520_FLAG1_RST1_MASK;sample();
    assert(!sh3673510_backend_get_feature_snapshot(&snap));
    puts("Every read/cell failure invalidates the sample without partial measurement publication");
    return 0;
}
#elif !defined(GUARD_TEST)
int main(void) {
    bms_afe_aux_measurements_t aux, previous;
    sh3673510_bms_afe_init();
    sample(); sample();
    assert(!sh3673510_bms_afe_get_aux_measurements(&aux));
    assert(!bms_error_get(BMS_ERROR_AFE1)); /* Waiting for first conversion is normal. */
    sample();
    assert(bms_error_get(BMS_ERROR_AFE1)); /* Bounded waiting, even if all reads ACK. */
    sh3673510_bms_afe_init();
    sh3673510_bms_afe_set_output_enabled(1);
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    device.flag1=SH3673520_FLAG1_OCC_MASK;
    for(unsigned i=0;i<20;++i) sample();
    assert(clear_calls==0); /* MOS-off zero current is not charger removal. */
    assert(command_c==0);
    puts("OCC stays latched while charger is connected and measured current is zero");
    cplus_raw=384;sample();assert(clear_calls==0); /* Hysteresis is not removal. */
    cplus_raw=0xffff;sample();assert(clear_calls==0); /* Invalid ADC is not zero. */
    uint8_t present;
    assert(!sh3673510_backend_get_charge_source_present(&present));
    cplus_raw=0;
    sample();
    assert(clear_calls==1); /* A settled C+ low sample permits recovery. */
    assert(command_c==0); /* Still inhibited until a later flag readback. */
    sample(); assert(command_c==1);

    device.flag1=SH3673520_FLAG1_OCD1_MASK;
    device.bstatus2=SH3673520_BSTATUS2_LOADOFF_MASK;
    unsigned before=clear_calls;
    sample(); assert(load_mode==1 && clear_calls==before);
    for(unsigned i=0;i<9;++i) sample();
    assert(clear_calls==before);
    sample(); assert(clear_calls==before+1 && command_d==0);
    sample(); assert(command_d==1 && load_mode==0);

    /* Simultaneous SC/OCC: finish LOAD recovery before obtaining C+ evidence. */
    device.flag1=SH3673520_FLAG1_SC_MASK|SH3673520_FLAG1_OCC_MASK;
    before=clear_calls;sample();
    assert(!sh3673510_backend_get_charge_source_present(&present));
    for(unsigned i=0;i<10;++i)sample();
    assert(clear_calls==before+1 && device.flag1==SH3673520_FLAG1_OCC_MASK);
    sample();assert(load_mode==1 && command_c==0); /* SC readback confirmation */
    sample();assert(load_mode==0 && command_c==0); /* C+ settle begins */
    sample();assert(clear_calls==before+2 && command_c==0);
    sample();assert(command_c==1);

    assert(sh3673510_bms_afe_get_aux_measurements(&previous));
    device.flag2=0;
    sample(); sample();
    assert(sh3673510_bms_afe_get_aux_measurements(&aux));
    assert(aux.sample_tick_32k==previous.sample_tick_32k);
    sample(); assert(!sh3673510_bms_afe_get_aux_measurements(&aux));
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    for(unsigned i=0;i<8;++i) sample();
    mode_ok=0;sample();assert(!sh3673510_bms_afe_get_aux_measurements(&aux));
    assert(command_c==0 && command_d==0);
    mode_ok=1;sample();sample();

    /* A failed W0C write is an invalid sample, not a healthy retry loop. */
    device.flag1=SH3673520_FLAG1_OCC_MASK;
    clear_ok=0;
    sample();
    assert(!sh3673510_bms_afe_get_aux_measurements(&aux));
    assert(bms_error_get(BMS_ERROR_AFE1) && command_c==0 && command_d==0);
    puts("C+/LOAD mode settling, readback and failed-clear inhibit");
    return 0;
}
#else
int main(void) {
    bms_afe_aux_measurements_t aux;
    /* Start near tick wrap, with genuine 4 Hz ADC cadence at 200 ms polls. */
    now=0xffff8000u;
    bms_afe_init(); bms_afe_set_output_enabled(1); bms_afe_set_fets(1,1);
    for(unsigned i=1;i<=7;++i) {
        device.flag2=SH3673520_FLAG2_VADC_MASK;
        if(i%5!=1)device.flag2|=SH3673520_FLAG2_CADC_MASK;
        sample(); assert(!bms_afe_samples_qualified());
        assert(command_c==0 && command_d==0);
    }
    device.flag2|=SH3673520_FLAG2_CADC_MASK;
    sample(); assert(!bms_afe_samples_qualified());
    sample(); assert(bms_afe_samples_qualified());
    assert(command_c==1 && command_d==1 && bms_afe_get_aux_measurements(&aux));
    uint32_t stamp=aux.sample_tick_32k;
    device.flag2=SH3673520_FLAG2_VADC_MASK;
    sample();assert(bms_afe_get_aux_measurements(&aux) && aux.sample_tick_32k==stamp);
    sample();assert(bms_afe_samples_qualified());
    sample();assert(!bms_afe_samples_qualified() && command_c==0 && command_d==0);
    sample();assert(!bms_afe_bus_access_allowed());
    /* No status/clear/detection I/O during the watchdog silence window. */
    unsigned before=clear_calls;
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    device.flag1=SH3673520_FLAG1_OCC_MASK; cplus_raw=0;
    for(unsigned i=0;i<175;++i) { sample(); assert(!bms_afe_bus_access_allowed()); }
    assert(clear_calls==before);
    sample(); assert(bms_afe_bus_access_allowed() && !bms_afe_samples_qualified());
    for(unsigned i=0;i<8;++i)sample();
    assert(bms_afe_samples_qualified());
    device.flag1=SH3673520_FLAG1_OCC_MASK;clear_ok=0;
    sample(); assert(!bms_afe_samples_qualified() && command_c==0 && command_d==0);
    sample(); assert(!bms_afe_bus_access_allowed());
    puts("Three actual conversions, tick wrap, stale ADC shutdown and watchdog silence");
    return 0;
}
#endif
