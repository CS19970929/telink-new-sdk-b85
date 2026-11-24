"""参数块读取：每请求一次快照、7-word 客户端、边界及失败值保持。"""
from validation_support import read, function, run_c

source = read('bms/core/bms_parameter_access.c')
body = function(source, 'int bms_parameter_readable(') + '\n'
body += source[
    source.index('/* 仅在本次同步读取中存在；'):source.index('/* 校验并写入业务参数')]
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_product.h"
#include "bms_parameter_access.h"
#include "bms_config_store.h"
#include "bms_soc_app.h"
#include "bms_state.h"
#include "bms_sleep_config.h"
#include "bms_features.h"
#include "bms_afe.h"
#include "bms_update_policy.h"
static u16 s_sequence=123u,s_result=4u,s_sn_generation=5u;
static unsigned user_calls,system_calls,limit_calls,sample_calls,sleep_calls;
static u8 user_ok=1u,system_ok=1u,sample_ok=1u,limits_ok=1u;
static u32 now=0x100u, sample_tick=0xfffffff0u;
bms_report_t g_bms_report;
int bms_config_get_user(bms_user_params_t *v) {
    ++user_calls;memset(v,0,sizeof(*v));v->heater_enable=1u;v->heater_start_x10=390u;v->heater_stop_x10=450u;
    v->balance_enable=1u;v->balance_start_mv=3400u;v->balance_start_delta_mv=50u;v->balance_stop_delta_mv=30u;
    v->current_offset_ma=-321;v->current_gain_ppm=1234567u;
    memcpy(v->serial,"0123456789abcdefghijklmnopqrstuv",32u);return user_ok;
}
int bms_config_store_get_system(bms_config_system_params_t *v) {
    ++system_calls;memset(v,0,sizeof(*v));v->capacity_factory=1234u;return system_ok;
}
int bms_config_get_soc_limits(bms_soc_limits_t *limits) { ++limit_calls; *limits = (bms_soc_limits_t){3500u,2850u}; return limits_ok; }
u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *v) {
    ++sample_calls;v->raw_current_ma=INT32_MIN+123;v->current_ma=-1234;v->sample_tick_32k=sample_tick;v->sample_time_ms=sample_tick;return sample_ok;
}
void bms_config_get_sleep(bms_sleep_config_t *v) { ++sleep_calls;*v=(bms_sleep_config_t){2600,1,3300,2,30}; }
void bms_sleep_config_defaults(bms_sleep_config_t *v) { ++sleep_calls;*v=(bms_sleep_config_t){2700,3,3400,4,30}; }
u8 bms_board_heater_supported(void) { return 1u; }
u8 bms_soc_app_percent(void) { return 57u; }
uint32_t bms_time_ms(void) { return now; }
'''
tail = r'''
static u8 data[250];
static u16 value(unsigned i) { return (u16)(((u16)data[i*2u]<<8)|data[i*2u+1u]); }
static void reset(void) { user_calls=system_calls=limit_calls=sample_calls=sleep_calls=0u;memset(data,0xa5,sizeof(data)); }
static void check(u16 reg,u16 qty,const u16 *expected) {
    reset();assert(bms_parameter_read_block(reg,qty,data));
    for(unsigned i=0;i<qty;++i)assert(value(i)==expected[i]);
    assert(user_calls<=1u && system_calls<=1u && sample_calls<=1u && sleep_calls<=1u);
}
int main(void) {
    u16 expected[16];
    const u16 limits[] = {3500u,2850u};
    check(BMS_PARAM_REG_SOC100_MV,2u,limits);
    assert(limit_calls <= 2u);
    limits_ok=0u;
    const u16 unavailable[] = {0xFFFFu,0xFFFFu};
    check(BMS_PARAM_REG_SOC100_MV,2u,unavailable);
    limits_ok=1u;
    const char *serial="0123456789abcdefghijklmnopqrstuv";
    for(unsigned i=0;i<16u;++i)expected[i]=(u16)(((u16)(u8)serial[i*2u]<<8)|(u8)serial[i*2u+1u]);
    check(BMS_PARAM_REG_SN_BASE,16u,expected);assert(user_calls==1u);
    const u16 heat[]={1,390,450}, balance[]={1,3400,50,30}, chemistry[]={BMS_PRODUCT_CHEMISTRY};
    check(BMS_PARAM_REG_HEATER_ENABLE,3u,heat);check(BMS_PARAM_REG_BALANCE_ENABLE,4u,balance);
    check(BMS_PARAM_REG_CHEMISTRY,1u,chemistry);assert(system_calls==0u);
    const u16 sleep[]={2600,1,3300,2,30}, defaults[]={2700,3,3400,4,30};
    check(BMS_PARAM_REG_SLEEP_BASE,5u,sleep);check(BMS_PARAM_REG_SLEEP_DEFAULT_BASE,5u,defaults);
    const u16 currents[]={ (u16)(u32)-321,(u16)((u32)-321>>16), (u16)1234567u,(u16)(1234567u>>16),
        123u,0x8000u,(u16)(u32)-1234,(u16)((u32)-1234>>16),1u,0xfff0u,0xffffu};
    check(BMS_PARAM_REG_CURRENT_OFFSET_LO,11u,currents);assert(user_calls==1u&&sample_calls==1u);
    sample_ok=0u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_RAW_CURRENT_LO,7u,data));assert(!value(4));sample_ok=1u;
    sample_tick=now-BMS_SAMPLE_MAX_POLL_GAP_MS-1u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_SAMPLE_VALID,1u,data));assert(value(0)==1u);
    user_ok=0u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_SN_BASE,16u,data));assert(user_calls==1u);
    for(unsigned i=0;i<16u;++i)assert(value(i)==0xffffu);
    user_ok=1u;
    system_ok=0u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_CAPACITY,1u,data));assert(system_calls==1u&&value(0)==0xffffu);system_ok=1u;
    const u16 bad[][2]={{0xffff,2},{BMS_PARAM_REG_SN_LAST,2},{BMS_PARAM_REG_SOC0_MV,2},{0x2e00,0},{0x2e00,126}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        reset();assert(!bms_parameter_read_block(bad[i][0],bad[i][1],data));
        assert(!user_calls&&!system_calls&&!limit_calls&&!sample_calls&&!sleep_calls);
        for(unsigned j=0;j<sizeof(data);++j)assert(data[j]==0xa5u);
    }
    assert(!bms_parameter_read_block(BMS_PARAM_REG_MAGIC,1u,0));
    puts("PASS parameter block: two endpoint words, SN, calibration, chemistry, sleep, failure sentinels, wrap, invalid tail without reads");
    return 0;
}
'''
if __name__ == '__main__':
    run_c(prefix + body + tail, name='parameter-read-block')
