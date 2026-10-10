#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "dvc1124.h"
#include "bms_afe.h"
#include "bms_afe_driver.h"
#include "bms_features.h"
#include "bms_diag.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_protection_params.h"

#define DVC_MEAS_BYTES (DVC1124_REG_CELL24_L + 1u)
static dvc1124_snapshot_t s_snapshot;
static dvc1124_config_t s_cfg = {.cell_count=4, .battery_ntc_gp=2, .mos_ntc_gp=4};
static dvc1124_openwire_result_t s_openwire_result;
static uint32_t s_snapshot_generation, now;
static uint8_t s_need_config, registers[256], read_ok=1;
bms_protection_params_t g_bms_protection_params;
/* PRODUCTION STATE */

uint8_t bms_protection_params_valid(void) { return 1u; }
void bms_features_get_status(bms_features_status_t *s) { memset(s,0,sizeof(*s)); }
uint8_t bms_features_charge_direction_blocked(void) { return 0u; }
void bms_diag_driver(uint8_t flags,uint8_t valid) { (void)flags; (void)valid; }
void bms_diag_command(uint8_t command,uint8_t valid) { (void)command; (void)valid; }
void DVC1124_UpdataAfeConfig(void) {}
void DVC1124_OpenWirePoll(void) {}
uint8_t DVC1124_GetWriteAddress(void) { return 0x40u; }
void DVC1124_GetSnapshot(dvc1124_snapshot_t *s) { *s=s_snapshot; }
void DVC1124_GetConfig(dvc1124_config_t *c) { *c=s_cfg; }
static uint32_t pm_get_32k_tick(void) { return now; }
static void dvc_note_comm_result(uint8_t ok) { (void)ok; }
static uint16_t dvc_be16(const uint8_t *p) { return ((uint16_t)p[0]<<8)|p[1]; }
static int32_t dvc_sign_extend20(uint32_t x) { return (x&0x80000u)?(int32_t)(x|0xfff00000u):(int32_t)x; }
static int32_t dvc_cc2_to_raw_current_ma(int32_t x) { return x; }
static int32_t bms_config_calibrate_current(int32_t x) { return x; }
static int32_t dvc_apply_boot_zero(int32_t x) { return x; }
static void dvc_publish_current_report(int32_t x) { (void)x; }
static uint16_t dvc_correct_cell_mv(uint16_t x,uint32_t cm) { (void)cm; return x; }
static uint8_t dvc_refresh_balance_state(void) { return 1u; }
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *data,uint8_t count)
{
    if (!read_ok) return 0u;
    memcpy(data,registers+reg,count);
    if (reg==DVC1124_REG_ALARM && count==DVC_MEAS_BYTES) {
        s_pending_adc_events |= registers[DVC1124_REG_STATUS]&0x50u;
        registers[DVC1124_REG_STATUS] &= (uint8_t)~0x50u;
    }
    return 1u;
}
uint8_t DVC1124_WriteRegisterSafe(uint8_t reg,uint8_t requested)
{
    assert(reg==DVC1124_REG_FET_CTRL);
    registers[reg]=requested;
    return 1u;
}

/* PRODUCTION */

static void put16(uint8_t reg,uint16_t value)
{
    registers[reg]=(uint8_t)(value>>8);
    registers[reg+1u]=(uint8_t)value;
}
static void reset(void)
{
    memset(&s_snapshot,0,sizeof(s_snapshot));
    memset(&g_bms_report,0,sizeof(g_bms_report));
    memset(&g_bms_protection_params,0,sizeof(g_bms_protection_params));
    memset(registers,0,sizeof(registers));
    memset(s_ntc_valid_samples,0,sizeof(s_ntc_valid_samples));
    s_pending_adc_events=s_voltage_seen=s_current_seen=s_sample_pending=s_voltage_since_current=0u;
    s_snapshot_generation=s_voltage_tick=s_current_tick=s_adc_wait_started=0u;
    read_ok=1u; now=1000u; s_snapshot.rpu_ohm=10000u;
    put16(DVC1124_REG_V1P8_H,18000u);
    for (unsigned gp=0;gp<4u;++gp) put16((uint8_t)(DVC1124_REG_GP1_H+2u*gp),9000u);
    g_bms_protection_params.mos_otp_first_x10=1500u;
    g_bms_protection_params.mos_otp_second_x10=1500u;
    g_bms_protection_params.mos_otp_third_x10=1500u;
    g_bms_protection_params.mos_otp_recover_x10=1400u;
    bms_sw_protection_init();
}
static void step(uint8_t events,uint32_t elapsed_ticks)
{
    bms_sw_protection_inputs_t in={0};
    bms_afe_feature_snapshot_t feature;
    now+=elapsed_ticks; registers[DVC1124_REG_STATUS]=events;
    DVC1124_App_AFEGet();
    if (!s_snapshot.valid) return; /* 通信隔离由独立 guard 覆盖，本夹具不伪造它。 */
    in.battery_temp_valid=dvc_get_battery_temperature_range(
        &s_snapshot,&in.battery_temp_min,&in.battery_temp_max);
    in.mos_temp_required=1u;
    in.mos_temp_valid=dvc_configured_ntc_valid(&s_snapshot,4u);
    in.mos_temp=dvc_get_configured_temperature(4u);
    dvc_publish_temperature_report(&in);
    bms_sw_protection_update_groups(&in,0u,1u);
    assert(dvc1124_backend_get_feature_snapshot(&feature));
    assert(feature.battery_temp_valid==in.battery_temp_valid);
    assert(feature.mos_temp_valid==in.mos_temp_valid);
    assert(dvc_apply_common_port_fet_state(1u,1u));
}
static void check_closed(void)
{
    assert(bms_error_get(BMS_ERROR_TEMP_BREAK));
    assert(bms_sw_protection_charge_blocked() && bms_sw_protection_discharge_blocked());
    assert((registers[DVC1124_REG_FET_CTRL]&0x0fu)==0u);
}
static void check_open(void)
{
    assert(!bms_error_get(BMS_ERROR_TEMP_BREAK));
    assert(!bms_sw_protection_charge_blocked() && !bms_sw_protection_discharge_blocked());
    assert((registers[DVC1124_REG_FET_CTRL]&0x0fu)==0x0fu);
}
static void qualify(void)
{
    step(0x50u,6400u); check_closed();
    step(0x50u,6400u); check_closed();
    step(0x50u,6400u); check_open();
    assert(g_bms_report.temperature_min_x10==650u && g_bms_report.temperature_max_x10==650u);
}
/* 复用本夹具的真实公共保护和 DVC 仲裁，覆盖压差到 R81 的双向关断。 */
static void check_cell_delta_protection(void)
{
    bms_sw_protection_inputs_t in={0};
    reset(); qualify();
    in.battery_temp_valid=in.mos_temp_valid=in.mos_temp_required=1u;
    in.battery_temp_min=in.battery_temp_max=in.mos_temp=650u;
    g_bms_protection_params.cell_delta_first_mv=600u;
    g_bms_protection_params.cell_delta_second_mv=800u;
    g_bms_protection_params.cell_delta_third_mv=1000u;
    g_bms_protection_params.cell_delta_recover_mv=800u;
    g_bms_protection_params.cell_delta_filter_10ms=100u;
    g_bms_report.charge_current_a10=10u;
    g_bms_report.cell_delta_mv=900u;
    for (unsigned i=0u;i<5u;++i) bms_sw_protection_update(&in);
    assert(g_bms_report.fault_second.bits.cell_delta_high);
    assert(dvc_apply_common_port_fet_state(1u,1u)); check_open();

    g_bms_report.cell_delta_mv=3300u; /* 有效串位掉到 0，其他串约 3300 mV。 */
    for (unsigned i=0u;i<5u;++i) {
        bms_sw_protection_update(&in);
        assert(dvc_apply_common_port_fet_state(1u,1u));
        assert((registers[DVC1124_REG_FET_CTRL]&0x0fu)==(i<4u?0x0fu:0u));
    }
    assert(bms_sw_protection_charge_blocked() && bms_sw_protection_discharge_blocked());
    for (unsigned request=0u;request<4u;++request) {
        assert(dvc_apply_common_port_fet_state(request&1u,(request>>1)&1u));
        assert((registers[DVC1124_REG_FET_CTRL]&0x0fu)==0u);
    }
    in.voltage_sample_diagnostic=1u; g_bms_report.cell_delta_mv=0u;
    for (unsigned i=0u;i<6u;++i) bms_sw_protection_update(&in);
    assert(g_bms_report.fault_third.bits.cell_delta_high);
    in.voltage_sample_diagnostic=0u; g_bms_report.cell_delta_mv=801u;
    for (unsigned i=0u;i<6u;++i) bms_sw_protection_update(&in);
    assert(g_bms_report.fault_third.bits.cell_delta_high);
    g_bms_report.cell_delta_mv=800u;
    for (unsigned i=0u;i<5u;++i) {
        bms_sw_protection_update(&in);
        assert(dvc_apply_common_port_fet_state(1u,1u));
        assert((registers[DVC1124_REG_FET_CTRL]&0x0fu)==(i<4u?0u:0x0fu));
    }
    check_open();
}

int main(void)
{
    uint32_t resistance;
    /* 独立整除向量：100 ohm 和 500 kohm 边界；无效不得留下非零电阻。 */
    assert(dvc_ntc_resistance(100u,10100u,10000u,&resistance) && resistance==100u);
    assert(dvc_ntc_resistance(10000u,10200u,10000u,&resistance) && resistance==500000u);
    assert(!dvc_ntc_resistance(99u,10100u,10000u,&resistance) && !resistance);
    assert(!dvc_ntc_resistance(10001u,10200u,10000u,&resistance) && !resistance);
    assert(!dvc_ntc_resistance(9000u,18000u,0u,&resistance) && !resistance);
    assert(!dvc_ntc_resistance(1u,0u,10000u,&resistance) && !resistance);
    for (unsigned frt=0;frt<256u;++frt) {
        unsigned rpu=6800u+frt*25u;
        uint16_t gp=(uint16_t)((uint64_t)18000u*3000000u/(3000000u+rpu));
        assert(!dvc_ntc_resistance(gp,18000u,(uint16_t)rpu,&resistance) && !resistance);
    }
    /* 每一路必需 NTC、两路电池同时拔除：持续故障不能开 MOS。 */
    for (unsigned channel=1u;channel<=4u;++channel) {
        static const uint16_t open_codes[]={17940u,18000u,17999u,18001u,0u,90u};
        reset(); qualify();
        for (unsigned n=0;n<48u;++n) {
            unsigned gp=(channel==4u)?1u:channel;
            put16((uint8_t)(DVC1124_REG_GP1_H+2u*gp),open_codes[n%6u]);
            if (channel==4u) put16(DVC1124_REG_GP3_H,open_codes[(n+1u)%6u]);
            step(0x50u,6400u); check_closed();
        }
        /* 插回只恢复对应通道；CC2/缓存轮询不能代替温度新转换。 */
        for (unsigned gp=0;gp<4u;++gp) put16((uint8_t)(DVC1124_REG_GP1_H+2u*gp),9000u);
        step(0x10u,100u); check_closed();
        step(0x50u,6400u); check_closed();
        for (unsigned n=0;n<8u;++n) { step(0x10u,100u); check_closed(); }
        step(0x50u,6400u); check_closed();
        step(0x50u,6400u); check_open();
    }
    /* 两个正常新转换之间再次断线，恢复资格必须从头开始。 */
    reset(); qualify(); put16(DVC1124_REG_GP2_H,17940u); step(0x50u,6400u); check_closed();
    put16(DVC1124_REG_GP2_H,9000u); step(0x50u,6400u); step(0x50u,6400u); check_closed();
    put16(DVC1124_REG_GP2_H,18000u); step(0x50u,6400u); check_closed();
    put16(DVC1124_REG_GP2_H,9000u); qualify();
    /* 短暂读失败、长采样空档均撤销温度资格；随后需三个新转换。 */
    read_ok=0u; step(0x50u,6400u); assert(!s_snapshot.valid && !s_snapshot.ntc_res_ohm[1]);
    read_ok=1u; qualify();
    step(0x50u,25600u); check_closed();
    step(0x50u,6400u); check_closed(); step(0x50u,6400u); check_open();
    reset(); now=0xffffb000u; qualify();
    put16(DVC1124_REG_GP3_H,17940u); step(0x50u,6400u); check_closed();
    put16(DVC1124_REG_GP3_H,9000u); qualify();
    /* GP1 是加热探头，其失效不等同于电池/MOS 必需温度失效。 */
    put16(DVC1124_REG_GP1_H,17940u); step(0x50u,6400u); check_open();
    assert(!s_snapshot.ntc_res_ohm[0]);
    check_cell_delta_protection();
    puts("PASS D008 NTC 开短路、两电池探头/MOS 探头、三次新 VADF 恢复、缓存/通信空档、tick 回绕及三级压差双向关断/恢复");
    return 0;
}
