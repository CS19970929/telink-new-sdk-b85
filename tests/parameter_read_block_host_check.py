"""参数块读取：每请求一次快照、7-word 客户端、边界及失败值保持。"""
from validation_support import read, function, run_c

source = read('bms/core/bms_parameter_access.c')
body = function(source, 'int bms_parameter_readable(') + '\n'
body += source[
    source.index('/* 仅在本次同步读取中存在；'):source.index('/* 完整短帧校验密码')]
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_product.h"
#include "bms_parameter_access.h"
#include "bms_config_store.h"
#include "bms_soc_profile.h"
#include "bms_sleep_config.h"
#include "bms_features.h"
#include "bms_afe.h"
#include "bms_update_policy.h"
static u16 s_sequence=123u,s_result=4u,s_sn_generation=5u,s_soc_profile_generation=6u;
static u16 s_chemistry_status=2u,s_soc_profile_stage_status=1u;
static unsigned user_calls,system_calls,profile_calls,sample_calls,sleep_calls;
static u8 user_ok=1u,system_ok=1u,sample_ok=1u;
static bms_soc_profile_config_t active_profile;
static u32 now=0x100u, sample_tick=0xfffffff0u;
bms_soc_state_t g_bms_soc;
int bms_config_get_user(bms_user_params_t *v) {
    ++user_calls;memset(v,0,sizeof(*v));v->heater_enable=1u;v->heater_start_x10=390u;v->heater_stop_x10=450u;
    v->balance_enable=1u;v->balance_start_mv=3400u;v->balance_start_delta_mv=50u;v->balance_stop_delta_mv=30u;
    v->current_offset_ma=-321;v->current_gain_ppm=1234567u;
    memcpy(v->serial,"0123456789abcdefghijklmnopqrstuv",32u);return user_ok;
}
int bms_config_store_get_system(bms_config_system_params_t *v) {
    ++system_calls;memset(v,0,sizeof(*v));v->battery_chemistry=2u;v->soc_profile_id=2u;v->capacity_factory=1234u;return system_ok;
}
void bms_config_get_soc_profile(bms_soc_profile_config_t *v) { ++profile_calls;*v=active_profile; }
u16 bms_soc_profile_word(const bms_soc_profile_config_t *v,u16 offset) {
    u16 word=0u;if(offset<50u)memcpy(&word,(const u8*)v+offset*2u,2u);return word;
}
u8 bms_config_soc_profile_is_persisted(void) { return 1u; }
u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *v) {
    ++sample_calls;v->raw_current_ma=INT32_MIN+123;v->current_ma=-1234;v->sample_tick_32k=sample_tick;return sample_ok;
}
void bms_config_get_sleep(bms_sleep_config_t *v) { ++sleep_calls;*v=(bms_sleep_config_t){2600,1,3300,2,30}; }
void bms_sleep_config_defaults(bms_sleep_config_t *v) { ++sleep_calls;*v=(bms_sleep_config_t){2700,3,3400,4,30}; }
u8 bms_board_heater_supported(void) { return 1u; }
u8 get_soc_real(void) { return 57u; }
uint32_t bms_time_now_32k(void) { return now; }
'''
tail = r'''
static u8 data[250];
static u16 value(unsigned i) { return (u16)(((u16)data[i*2u]<<8)|data[i*2u+1u]); }
static void reset(void) { user_calls=system_calls=profile_calls=sample_calls=sleep_calls=0u;memset(data,0xa5,sizeof(data)); }
static void check(u16 reg,u16 qty,const u16 *expected) {
    reset();assert(bms_parameter_read_block(reg,qty,data));
    for(unsigned i=0;i<qty;++i)assert(value(i)==expected[i]);
    assert(user_calls<=1u && system_calls<=1u && profile_calls<=1u && sample_calls<=1u && sleep_calls<=1u);
}
int main(void) {
    for(unsigned i=0;i<50u;++i){u16 v=(u16)(0x1000u+i);memcpy((u8*)&active_profile+i*2u,&v,2u);}
    u16 expected[50];for(unsigned i=0;i<50u;++i)expected[i]=(u16)(0x1000u+i);
    check(BMS_PARAM_REG_SOC_PROFILE_BASE+2u,50u,expected);assert(profile_calls==1u);
    /* 现有 Windows/Android 7-word 分段，末段为 1 word。 */
    for(unsigned i=0;i<50u;i+=7u){unsigned n=50u-i<7u?50u-i:7u;check((u16)(BMS_PARAM_REG_SOC_PROFILE_BASE+2u+i),(u16)n,expected+i);assert(profile_calls==1u);}
    const char *serial="0123456789abcdefghijklmnopqrstuv";
    for(unsigned i=0;i<16u;++i)expected[i]=(u16)(((u16)(u8)serial[i*2u]<<8)|(u8)serial[i*2u+1u]);
    check(BMS_PARAM_REG_SN_BASE,16u,expected);assert(user_calls==1u);
    const u16 heat[]={1,390,450}, balance[]={1,3400,50,30}, chemistry[]={0x4348,1,2,2,2,0x1004,0x1005,0x1006};
    check(BMS_PARAM_REG_HEATER_ENABLE,3u,heat);check(BMS_PARAM_REG_BALANCE_ENABLE,4u,balance);
    check(BMS_PARAM_REG_CHEMISTRY_BASE,8u,chemistry);assert(system_calls==1u&&profile_calls==1u);
    const u16 sleep[]={2600,1,3300,2,30}, defaults[]={2700,3,3400,4,30};
    check(BMS_PARAM_REG_SLEEP_BASE,5u,sleep);check(BMS_PARAM_REG_SLEEP_DEFAULT_BASE,5u,defaults);
    const u16 currents[]={ (u16)(u32)-321,(u16)((u32)-321>>16), (u16)1234567u,(u16)(1234567u>>16),
        123u,0x8000u,(u16)(u32)-1234,(u16)((u32)-1234>>16),1u,0xfff0u,0xffffu};
    check(BMS_PARAM_REG_CURRENT_OFFSET_LO,11u,currents);assert(user_calls==1u&&sample_calls==1u);
    sample_ok=0u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_RAW_CURRENT_LO,7u,data));assert(!value(4));sample_ok=1u;
    sample_tick=now-BMS_SOC_MAX_SAMPLE_GAP_32K-1u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_SAMPLE_VALID,1u,data));assert(!value(0));
    user_ok=0u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_SN_BASE,16u,data));assert(user_calls==1u);
    for(unsigned i=0;i<16u;++i)assert(value(i)==0xffffu);
    user_ok=1u;
    system_ok=0u;reset();assert(bms_parameter_read_block(BMS_PARAM_REG_CHEMISTRY_BASE+2u,2u,data));assert(system_calls==1u&&value(0)==0xffffu&&value(1)==0xffffu);system_ok=1u;
    const u16 bad[][2]={{0xffff,2},{BMS_PARAM_REG_SN_LAST,2},{BMS_PARAM_REG_SOC_PROFILE_STAGE_STATUS,2},{0x2e00,0},{0x2e00,126}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        reset();assert(!bms_parameter_read_block(bad[i][0],bad[i][1],data));
        assert(!user_calls&&!system_calls&&!profile_calls&&!sample_calls&&!sleep_calls);
        for(unsigned j=0;j<sizeof(data);++j)assert(data[j]==0xa5u);
    }
    assert(!bms_parameter_read_block(BMS_PARAM_REG_MAGIC,1u,0));
    puts("PASS parameter block: 50 words/7-word chunks, SN, calibration, chemistry, sleep, failure sentinels, wrap, invalid tail without reads");
    return 0;
}
'''
if __name__ == '__main__':
    run_c(prefix + body + tail, name='parameter-read-block')
