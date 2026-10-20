#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
/* 本夹具断言 AFE 失败现场，明确选择服务诊断构建。 */
#ifndef BMS_SERVICE_DIAG_ENABLE
#define BMS_SERVICE_DIAG_ENABLE 1
#endif
#include "bms_state.h"
#include "bms_afe_driver.h"
#include "bms_afe_registers.h"
#include "sh3673510_diag.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "bms_error.h"
#include "bms_diag.h"
#include "bms_product.h"
#include "bms_soc.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673510_ntc.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
/* PROFILE TYPE */
const uint16_t sh3673510_ntc_10k[60] = {0};
bms_report_t g_bms_report;
/* Only the report bits used by the selected backend are platform-owned. */
volatile bms_system_status_t g_bms_system_status;
static uint8_t errors[BMS_ERROR_COUNT];
static uint32_t now;
static uint16_t failure_step, failure_flag2;
void bms_diag_afe_failure(uint16_t stage,int16_t error,uint16_t flag2,
    uint32_t elapsed,uint32_t crc,uint32_t retries) {
    (void)error;(void)elapsed;(void)crc;(void)retries;
    failure_step=stage;failure_flag2=flag2;
}
/* 本夹具验证采样/恢复；MOS 原始状态的诊断存储由独立 diag 回归覆盖。 */
void bms_diag_mos_raw_status(uint16_t raw_status) { (void)raw_status; }
void bms_diag_driver(uint8_t driver_bits, uint8_t valid)
{ (void)driver_bits; (void)valid; }
#ifndef GUARD_TEST
void bms_afe_publish_fet_status(uint8_t driver_bits, uint8_t valid) { (void)driver_bits; (void)valid; }
#endif
static sh3673510_control_status_t device;
static unsigned clear_calls;
static uint8_t clear_ok = 1, command_c, command_d;
static uint16_t cplus_raw = 1024; /* 4 V, charger remains connected */
static uint8_t load_mode;
static int32_t public_current_ma;
static uint8_t mode_ok = 1;
static unsigned read_stage, fail_stage;
static unsigned cplus_reads;
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
/* 采样恢复夹具不排队诊断；仍完整提供新寄存器依赖，避免未解析符号。 */
#ifndef MODE_TEST
static uint8_t diagnostic_control;
sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *v){assert(reg==SH3673520_REG_SCONF3);*v=diagnostic_control;return SH3673520_OK;}
sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t v){assert(reg==SH3673520_REG_SCONF3);diagnostic_control=v;return SH3673520_OK;}
#endif
sh3673520_status_t SH3673520_ReadRegOnce(uint8_t reg,uint8_t *v){assert(reg==SH3673520_REG_FLAG3);*v=0u;return SH3673520_OK;}
uint8_t bms_error_get(bms_error_id_t id) { return errors[id]; }
void bms_error_raise(bms_error_id_t id) { errors[id] = 1; }
void bms_error_clear(bms_error_id_t id) { errors[id] = 0; }
#ifndef GUARD_TEST
uint16_t bms_afe_get_guard_diagnostic_bits(void) { return DIAG_GUARD_OUTPUT_ENABLED | DIAG_GUARD_SAMPLES_QUALIFIED; }
#else
void bms_features_init(void) {}
void bms_features_service(void) {}
void bms_features_stop(void) {}
uint8_t bms_protection_params_valid(void) { return 1; }
#endif
uint8_t bms_sw_protection_charge_blocked(void) { return 0; }
uint8_t bms_sw_protection_discharge_blocked(void) { return 0; }
uint8_t bms_features_charge_direction_blocked(void) { return 0; }
void bms_sw_protection_init(void) {}
void bms_sw_protection_reset_filters(void) {}
static bms_sw_protection_inputs_t last_sw_sample;
void bms_sw_protection_update(const bms_sw_protection_inputs_t *p) { last_sw_sample=*p; }
void bms_sw_protection_reset_current_recovery(void) {}
uint8_t bms_sw_protection_discharge_overcurrent_active(void) { return 0; }
void bms_sw_protection_clear(void) {}
void bms_sw_protection_record_fault_edges(void) {}
static int32_t bms_config_calibrate_current(int32_t ma) { return ma; }
static uint32_t __attribute__((unused)) pm_get_32k_tick(void) { return now; }
uint32_t bms_time_now_32k(void) { return now; }
uint32_t bms_time_ms(void){static uint32_t last,ms,fraction;uint32_t raw=now;uint32_t delta=raw-last;last=raw;fraction+=delta%32u;ms+=delta/32u+fraction/32u;fraction%=32u;return ms;}

uint16_t bms_lookup_u16(const uint16_t *p, uint16_t n, uint16_t x) { (void)p; (void)n; (void)x; return 650; }
uint8_t sh3673510_control_init(void) { load_mode = 0; return 1; }
uint8_t sh3673510_control_ready(void) { return 1; }
uint8_t sh3673510_control_wake(void) { return read_ok(); }
uint8_t sh3673510_control_sleep(void) { return 1; }
uint8_t sh3673510_control_apply_protection(void) { return 1; }
uint8_t sh3673510_control_set_fets(uint8_t c, uint8_t d) { command_c=c; command_d=d; return 1; }
uint8_t sh3673510_control_disable_fets(uint8_t c, uint8_t d) { if(c)command_c=0; if(d)command_d=0; return 1; }
uint8_t sh3673510_control_set_balance(uint32_t mask) { (void)mask; return 1; }
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
    p->enable_mask=BMS_AFE_HW_EN_TEMP;
    p->occ_recover_a10=10; p->ocd_recover_a10=10;
    p->occ_recover_ms=100; p->ocd_recover_ms=2000; return 1;
}
void SH3673520_GetCommStats(sh3673520_comm_stats_t *s) { memset(s,0,sizeof(*s)); }
sh3673520_status_t SH3673520_ReadCellVoltages(int32_t *v,uint8_t n) { for(uint8_t i=0;i<n;++i)v[i]=(i==bad_cell)?bad_cell_mv:3300; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_ReadPackVoltage(int32_t *v) { *v=3300*SH3673510_BOARD_CELL_COUNT; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_ReadCurrent(int32_t *v) { *v=0; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_CurrentRawToMilliAmpRatio(int32_t raw,uint32_t shunt,uint8_t parallel,int32_t *v) { (void)raw;(void)shunt;(void)parallel;*v=-public_current_ma; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_ReadTemperatures(int32_t *t) { for(uint8_t i=0;i<4;++i)t[i]=(i==invalid_ntc)?0:10000; return read_ok()?SH3673520_OK:SH3673520_ERR_SPI; }
sh3673520_status_t SH3673520_NtcRawToOhm(int32_t raw,uint32_t *v) { *v=(uint32_t)raw; return raw?SH3673520_OK:SH3673520_ERR_RANGE; }
sh3673520_status_t SH3673520_ReadRegs(uint8_t reg,uint8_t *v,sh3673520_size_t n) {
    /* 恢复场景不申请均衡；真实后端的关闭回读仍经过设备端口。 */
    if(reg==SH3673520_REG_BALANCEH) { assert(n==3);memset(v,0,n);return SH3673520_OK; }
    assert(reg==SH3673520_REG_VCHGRH && n==2);++cplus_reads;v[0]=(uint8_t)(cplus_raw>>8);v[1]=(uint8_t)cplus_raw;return read_ok()?SH3673520_OK:SH3673520_ERR_SPI;
}

/* PRODUCTION */
#ifdef GUARD_TEST
/* GUARD */
#endif

static bms_afe_sample_status_t last_sample_result;
static void sample(void) {
    now += 6400;
    read_stage=0;
#ifdef GUARD_TEST
    last_sample_result = bms_afe_sample();
    if (last_sample_result == BMS_AFE_SAMPLE_OK) bms_afe_apply_outputs();
#else
    last_sample_result = bms_afe_driver_sample();
    bms_afe_driver_set_fets(last_sample_result!=BMS_AFE_SAMPLE_ERROR,
                           last_sample_result!=BMS_AFE_SAMPLE_ERROR);
#endif
}
#if defined(MODE_TEST)
int main(void) {
    uint8_t changed;
    /* p32：负载检测保留 OWD_EN；自清 TRG 不决定是否重写。 */
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
    bms_afe_driver_init();
    for(unsigned i=0;i<8;++i)sample();
    assert(bms_afe_driver_get_aux_measurements(&aux));
    assert(bms_afe_driver_get_aux_measurements(NULL));
    assert(last_sample_result==BMS_AFE_SAMPLE_OK);
    assert(command_c==1u && command_d==1u);
#if SH3673510_PRODUCT_HEATER_SUPPORTED
    assert(cplus_reads>0u && read_stage==8u); /* D011 加热仍需充电器证据。 */
#else
    assert(cplus_reads==0u && read_stage==7u);
    uint8_t present;
    (void)present;
#if BMS_FEATURE_HEATER_SUPPORTED
    assert(!bms_afe_driver_get_charge_source_present(&present));
#endif
    /* 独立软件保护所有者已锁存 OCC 时，同样必须重新读取 C+。 */
    g_bms_report.fault_third.bits.charge_ocp=1u;
    sample(); assert(cplus_reads==1u);
    g_bms_report.fault_third.bits.charge_ocp=0u;
#endif
    bms_report_t before=g_bms_report;
    for(unsigned f=1;f<=7;++f) {
        /* Re-establish the public healthy state before each independent fault. */
        fail_stage=0;sample();sample();
        device.bstatus2=SH3673520_BSTATUS2_IDLE_MASK;
        fail_stage=f;sample();
        assert(last_sample_result==((f>=2 && f<=6)?BMS_AFE_SAMPLE_READ_ERROR:BMS_AFE_SAMPLE_ERROR));
        const uint16_t expected[]={0,DIAG_AFE_FAIL_WAKE,DIAG_AFE_FAIL_STATUS,
            DIAG_AFE_FAIL_CELL_READ,DIAG_AFE_FAIL_PACK_READ,DIAG_AFE_FAIL_CURRENT_READ,
            DIAG_AFE_FAIL_TEMP_READ,DIAG_AFE_FAIL_CURRENT_CONVERT,DIAG_AFE_FAIL_RELEASE_READ};
        if(failure_step!=expected[f]) fprintf(stderr,"failure stage f=%u expected=%u actual=%u\n",f,expected[f],failure_step);
        assert(failure_step==expected[f]);
        if(f>2u) assert(failure_flag2 & 0x200u);
        device.bstatus2=0u;
        assert(!bms_afe_driver_get_aux_measurements(&aux));
        assert(!memcmp(&before,&g_bms_report,sizeof(before)));
        assert(!bms_afe_driver_get_feature_snapshot(&snap));
    }
    /* 硬件 OCC 需要的 C+ 读取失败仍使整帧失效并关闭两路 MOS。 */
    fail_stage=0;sample();sample();
    assert(command_c==1u && command_d==1u);
    device.flag1=SH3673520_FLAG1_OCC_MASK;
    fail_stage=8u;sample();
    assert(failure_step==DIAG_AFE_FAIL_RELEASE_READ);
    assert(!bms_afe_driver_get_aux_measurements(&aux));
    assert(!bms_afe_driver_get_aux_measurements(NULL));
    assert(!memcmp(&before,&g_bms_report,sizeof(before)));
    assert(command_c==0u && command_d==0u);
    device.flag1=0u;
    fail_stage=0;
    for(unsigned c=0;c<SH3673510_BOARD_CELL_COUNT;++c)for(unsigned v=0;v<2;++v) {
        bad_cell=(int)c;bad_cell_mv=v?65536:-1;sample();
        assert(failure_step==DIAG_AFE_FAIL_CELL_RANGE);
        assert(!bms_afe_driver_get_aux_measurements(&aux));
        assert(!memcmp(&before,&g_bms_report,sizeof(before)));
    }
    bad_cell=-1;sample();sample();
    unsigned reads=read_stage;
    assert(bms_afe_driver_get_feature_snapshot(&snap) && snap.battery_temp_valid);
    assert(read_stage==reads && snap.cell_count==SH3673510_BOARD_CELL_COUNT);
    for(unsigned i=SH3673510_BOARD_CELL_COUNT;i<32;++i)assert(g_bms_report.cell_voltage_mv[i]==61001);
    invalid_ntc=SH3673510_BOARD_BAT_NTC1_INDEX;sample();
    assert(bms_afe_driver_get_feature_snapshot(&snap) && !snap.battery_temp_valid);
    device.flag1=SH3673520_FLAG1_RST1_MASK;sample();
    assert(!bms_afe_driver_get_feature_snapshot(&snap));
    puts("Required read/cell failures remain atomic; healthy heater-free products skip unused C+ reads");
    return 0;
}
#elif !defined(GUARD_TEST)
int main(void) {
    bms_afe_aux_measurements_t aux, previous;
    bms_afe_driver_init();
    sample(); sample();
    assert(last_sample_result==BMS_AFE_SAMPLE_NOT_READY);
    assert(!bms_afe_driver_get_aux_measurements(&aux));
    assert(!bms_afe_driver_get_aux_measurements(NULL));
    assert(!bms_error_get(BMS_ERROR_AFE1)); /* Waiting for first conversion is normal. */
    for(unsigned i=0;i<8;i++)sample();
    assert(last_sample_result==BMS_AFE_SAMPLE_OK);
    assert(!bms_error_get(BMS_ERROR_AFE1)); /* 无事件不再被当作停更故障。 */
    bms_afe_driver_init();
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    device.flag1=SH3673520_FLAG1_OCC_MASK;
    for(unsigned i=0;i<20;++i) sample();
    assert(clear_calls==0); /* MOS-off zero current is not charger removal. */
    assert(command_c==0);
    puts("OCC stays latched while charger is connected and measured current is zero");
    cplus_raw=384;sample();assert(clear_calls==0); /* Hysteresis is not removal. */
    cplus_raw=0xffff;sample();assert(clear_calls==0); /* Invalid ADC is not zero. */
    uint8_t present;
    (void)present;
#if BMS_FEATURE_HEATER_SUPPORTED
    assert(!bms_afe_driver_get_charge_source_present(&present));
#endif
    cplus_raw=0;
    sample();
    assert(clear_calls==1); /* A settled C+ low sample permits recovery. */
    assert(command_c==0); /* Still inhibited until a later flag readback. */
    sample(); assert(command_c==1);

    device.flag1=SH3673520_FLAG1_OCD1_MASK;
    device.bstatus2=SH3673520_BSTATUS2_LOADOFF_MASK;
    unsigned before=clear_calls;
    sample(); assert(load_mode==1 && clear_calls==before);
    for(unsigned i=0;i<10;++i) sample();
    assert(clear_calls==before);
    sample(); assert(clear_calls==before+1 && command_d==0);
    sample(); assert(command_d==1 && load_mode==0);

    /* Simultaneous SC/OCC: finish LOAD recovery before obtaining C+ evidence. */
    device.flag1=SH3673520_FLAG1_SC_MASK|SH3673520_FLAG1_OCC_MASK;
    before=clear_calls;sample();
    (void)present;
#if BMS_FEATURE_HEATER_SUPPORTED
    assert(!bms_afe_driver_get_charge_source_present(&present));
#endif
    for(unsigned i=0;i<11;++i)sample();
    assert(clear_calls==before+1 && device.flag1==SH3673520_FLAG1_OCC_MASK);
    sample();assert(load_mode==1 && command_c==0); /* SC readback confirmation */
    sample();assert(load_mode==0 && command_c==0); /* C+ settle begins */
    sample();assert(clear_calls==before+1 && command_c==0); /* 恰好200ms仍等待。 */
    sample();assert(clear_calls==before+2 && command_c==0);
    sample();assert(command_c==1);

    /* 完整公共采样路径：新鲜反向电流无需等待OCD恢复滤波或SC移除窗口。 */
    cplus_raw=1024;device.bstatus2=SH3673520_BSTATUS2_LOADON_MASK;
    device.flag1=SH3673520_FLAG1_OCD1_MASK|SH3673520_FLAG1_OCD2_MASK;
    before=clear_calls;public_current_ma=-200;sample();assert(clear_calls==before);
    public_current_ma=-201;device.flag2=SH3673520_FLAG2_VADC_MASK;
    sample();assert(clear_calls==before); /* 新CADC缺失，不能使用旧电流。 */
    assert(!last_sw_sample.current_recovery_sample_fresh && !last_sw_sample.discharge_recovery_immediate);
    device.flag2=SH3673520_FLAG2_CADC_MASK;
    sample();assert(clear_calls==before+1 && device.flag1==0);
    assert(last_sample_result==BMS_AFE_SAMPLE_OK);
    assert(last_sw_sample.current_recovery_sample_fresh && last_sw_sample.discharge_recovery_immediate);
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    sample();assert(command_d==1);
    device.flag1=SH3673520_FLAG1_SC_MASK;before=clear_calls;
    device.flag2=SH3673520_FLAG2_CADC_MASK;
    sample();assert(clear_calls==before+1 && command_d==0);
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    sample();assert(command_d==1 && !bms_error_get(BMS_ERROR_DSG_SHORT));
    device.flag1=SH3673520_FLAG1_OCC_MASK;before=clear_calls;
    device.flag2=SH3673520_FLAG2_CADC_MASK;
    public_current_ma=201;sample();assert(clear_calls==before+1 && device.flag1==0);
    assert(last_sw_sample.current_recovery_sample_fresh && last_sw_sample.charge_recovery_immediate);
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    sample();assert(command_c==1);
    public_current_ma=0;device.bstatus2=0;sample();sample();

    assert(bms_afe_driver_get_aux_measurements(&previous));
    device.flag2=0;
    sample(); sample();
    assert(bms_afe_driver_get_aux_measurements(&aux));
    assert(aux.sample_tick_32k!=previous.sample_tick_32k);
    for(unsigned i=0;i<100;i++)sample();
    assert(bms_afe_driver_get_aux_measurements(&aux) && last_sample_result==BMS_AFE_SAMPLE_OK);
    assert(command_c && command_d);
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    for(unsigned i=0;i<8;++i) sample();
    mode_ok=0;sample();assert(!bms_afe_driver_get_aux_measurements(&aux));
    assert(command_c==0 && command_d==0);
    mode_ok=1;sample();sample();

    /* A failed W0C write is an invalid sample, not a healthy retry loop. */
    device.flag1=SH3673520_FLAG1_OCC_MASK;
    public_current_ma=201; /* 新鲜放电触发 OCC 清除，随后注入 W0C 写失败。 */
    clear_ok=0;
    sample();
    assert(!bms_afe_driver_get_aux_measurements(&aux));
    assert(s_flag_clear_failed && command_c==0 && command_d==0);
    puts("C+/LOAD mode settling, readback and failed-clear inhibit");
    return 0;
}
#else
int main(void) {
    bms_afe_aux_measurements_t aux;
    /* 完整读取建立输出资格；无ADC事件不关MOS。 */
    now=0xffff8000u;device.flag2=0;
    bms_afe_init();bms_afe_set_output_enabled(1);bms_afe_set_fets(1,1);
    for(unsigned i=0;i<6;i++){sample();assert(!output_ready() && !command_c && !command_d);}
    sample();assert(output_ready() && command_c && command_d);
    device.flag1=SH3673520_FLAG1_RST1_MASK;
    sample();assert(!output_ready() && !s_guard.comm_failures);
    for(unsigned i=0;i<6;i++){sample();assert(!output_ready());}
    sample();assert(output_ready() && command_c && command_d);
    for(unsigned i=0;i<100;i++)sample();
    assert(output_ready() && bms_afe_get_aux_measurements(&aux) && !bms_error_get(BMS_ERROR_AFE1));
    /* 实际 SH + guard：方向临时放行后读失败，不应发出旧远程许可的 OFF。 */
    for(unsigned channel=0;channel<2;channel++) {
        device.bstatus2=channel?SH3673520_BSTATUS2_CHGING_MASK:SH3673520_BSTATUS2_DSGING_MASK;
        sample();assert(bms_afe_set_channel_allowed((uint8_t)channel,0));
        assert(command_c && command_d);
        fail_stage=4u;sample();assert(command_c && command_d);
        assert(bms_afe_set_channel_allowed((uint8_t)(1u-channel),0));
        assert(command_c==(channel==0u) && command_d==(channel==1u));
        fail_stage=0u;device.bstatus2=0u;sample();
        assert(!command_c && !command_d);
        assert(bms_afe_set_channel_allowed(0,1) && bms_afe_set_channel_allowed(1,1));
        sample();assert(command_c && command_d);
    }
    /* 明确的清锁存失败仍关闭并进入既有watchdog静默。 */
    device.flag1=SH3673520_FLAG1_OCC_MASK;
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    public_current_ma=201;clear_ok=0;
    sample();assert(!command_c && !command_d && bms_error_get(BMS_ERROR_AFE1));
    sample();assert(!bms_afe_bus_access_allowed());
    clear_ok=1;
    /* No status/clear/detection I/O during the watchdog silence window. */
    unsigned before=clear_calls;
    device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    device.flag1=SH3673520_FLAG1_OCC_MASK; cplus_raw=0;
    /* 高频调用不能提前结束静默；时间跨回绕后仍须完整等待。 */
    for(unsigned i=0;i<1000;++i) { bms_afe_sample(); assert(!bms_afe_bus_access_allowed()); }
    now += 35000u * 32u - 1u;
    if (bms_afe_sample() == BMS_AFE_SAMPLE_OK) bms_afe_apply_outputs(); assert(!bms_afe_bus_access_allowed());
    assert(clear_calls==before);
    now+=33u; bms_afe_sample(); assert(bms_afe_bus_access_allowed() && !((bms_afe_get_guard_diagnostic_bits() & DIAG_GUARD_SAMPLES_QUALIFIED) != 0u));
    for(unsigned i=0;i<9;++i)sample();
    assert(((bms_afe_get_guard_diagnostic_bits() & DIAG_GUARD_SAMPLES_QUALIFIED) != 0u));
    device.flag1=SH3673520_FLAG1_OCC_MASK;clear_ok=0;
    sample(); assert(!((bms_afe_get_guard_diagnostic_bits() & DIAG_GUARD_SAMPLES_QUALIFIED) != 0u) && command_c==0 && command_d==0);
    sample(); assert(!bms_afe_bus_access_allowed());
    puts("Complete-read readiness, no ADC health gate, tick wrap and watchdog silence");
    return 0;
}
#endif
