#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
/* Compile the whole production module, with no function extraction. */
#include "modbus_rtu.c"

bms_protection_params_t g_bms_protection_params;
uint8_t deepsleep_en;
static unsigned requested_reads, effective_reads, event_reads, frames;
static unsigned parameter_reads, manual_soc_writes;
static u16 manual_soc;
static u8 parameter_mixed;
static bms_user_params_t identity_user;
static int identity_user_ok=1;
static int effective_fail_at = -1;
static u8 requested_valid=1u;
static u16 requested_base=0x1000u;
static u16 effective_base = 0x2000u;
static uint32_t corpus = 2166136261u;
static u8 remote_allowed_mask=3u,requested_charge=1u,requested_discharge=1u;
static u8 fet_driver=3u,fet_driver_valid=1u;
uint32_t bms_time_now_32k(void) { return 123u; }
uint32_t bms_time_ms(void) { return 250u; }
uint8_t bms_afe_get_remote_allowed_mask(void) { return remote_allowed_mask; }
uint8_t bms_afe_set_channel_allowed(uint8_t channel,uint8_t allowed)
{
    u8 bit=(u8)(1u<<channel);
    if(channel>1u||allowed>1u)return 0u;
    if(allowed)remote_allowed_mask|=bit;else remote_allowed_mask&=(u8)~bit;
    return 1u;
}
void bms_afe_get_requested_fets(uint8_t*c,uint8_t*d){*c=requested_charge;*d=requested_discharge;}
uint8_t bms_afe_get_fet_status(uint8_t *bits){*bits=fet_driver;return fet_driver_valid;}
uint16_t bms_afe_get_output_status(void){return BMS_AFE_STATUS_OUTPUT_ENABLED|BMS_AFE_STATUS_SAMPLES_QUALIFIED;}

/* Register-owner spies: controlled reads; unexpected writes abort. */
static u8 profile(bms_afe_hw_profile_t *p, unsigned *calls, int fail_at, u16 base)
{
    for (unsigned i=0; i<BMS_AFE_HW_PROFILE_WORD_COUNT; ++i) {
        u16 word=(u16)(base+i);
        memcpy((u8 *)p+i*2u,&word,sizeof(word));
    }
    return (int)++*calls != fail_at;
}
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p)
{ (void)profile(p,&requested_reads,-1,requested_base); return requested_valid; }
u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *p)
{ return profile(p,&effective_reads,effective_fail_at,effective_base); }
u16 bms_afe_hw_profile_capabilities(void) { return 0x1234u; }
u16 bms_afe_hw_profile_apply_state(void) { return 2u; }
u16 bms_afe_hw_profile_last_error(void) { return 3u; }
uint16_t bms_features_status_word(uint16_t offset) { return (uint16_t)(0x3500u+offset); }
u16 bms_afe_hw_profile_diagnostic_word(u16 offset) { return (u16)(0x3400u+offset); }
u8 bms_afe_hw_access_is_active(void) { return 1u; }
u16 bms_event_log_read_reg(u16 r) { ++event_reads; return (u16)(0x4000u+r); }
const char *btname_get(void)
{
    static const char name[BTNAME_TOTAL_MAX_LEN + 1u] = "BT_TEST";
    return name;
}
int bms_parameter_readable(u16 r) { return (r>=0x2E00u && r<=0x2E0Fu) || (parameter_mixed && r==1u); }
int bms_parameter_read_block(u16 r,u16 n,u8 *d) {
    ++parameter_reads;
    for(unsigned i=0;i<n;++i){u16 v=(u16)((r+i)^0x55aau);d[i*2u]=(u8)(v>>8);d[i*2u+1u]=(u8)v;}
    return 1;
}
int bms_config_get_user(bms_user_params_t *u) { *u=identity_user; return identity_user_ok; }
void bms_config_store_get_default_protect(bms_protection_params_t *p) { (void)p; abort(); }
int bms_config_store_get_system(bms_config_system_params_t *p) { (void)p; abort(); }
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t *p) { (void)p; abort(); }
u8 bms_protection_params_commit(const bms_protection_params_t *p) { (void)p; abort(); }
u8 bms_protection_params_valid(void) { abort(); }
u8 bms_parameter_write(u16 r,u16 n,const u8 *p)
{
    assert(r == BMS_PARAM_REG_SOC && n == 1u);
    ++manual_soc_writes;
    manual_soc = (u16)((u16)p[0] << 8) | p[1];
    return 0u;
}
int btname_modbus_on_write_holding(u16 r,u16 n,const u16 *p)
{ (void)r; (void)n; (void)p; abort(); }
bms_afe_hw_error_t bms_afe_hw_profile_commit_be(const u8 *p,u16 n)
{ (void)p; (void)n; abort(); }
int bms_afe_hw_access_modbus_on_frame(const u8 *q,u32 n,u8 *r,u32 *l)
{ (void)q; (void)n; (void)r; (void)l; abort(); }

dvc1124_config_result_t DVC1124_ConfigServiceRead(dvc1124_config_field_t f,u32 *v)
{ *v=(u32)f; return DVC1124_CFG_OK; }
dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t f,u32 v)
{ (void)f; (void)v; abort(); }

/* Read-window support is decided by the single dispatch function; no separate table. */
static int addr_supported(u16 reg)
{
    u16 value;
    return read_reg(reg, &value);
}

/* Independent CRC oracle for both request and expected response. */
static u16 wire_crc(const u8 *p,unsigned n)
{
    u16 crc=0xffffu;
    for (unsigned i=0; i<n; ++i) {
        crc^=p[i];
        for (unsigned bit=0; bit<8; ++bit)
            crc=(u16)((crc>>1)^((crc&1u)?0xa001u:0u));
    }
    return crc;
}
static void expect_read(u8 addr,u16 reg,u16 qty,const u16 *words,u8 exception)
{
    u8 req[8]={addr,3u,(u8)(reg>>8),(u8)reg,(u8)(qty>>8),(u8)qty};
    u8 guarded[MODBUS_RTU_FRAME_CAPACITY+32u],expected[MODBUS_RTU_FRAME_CAPACITY];
    u32 length=0u;
    u16 crc=wire_crc(req,6u);
    req[6]=(u8)crc; req[7]=(u8)(crc>>8);
    memset(guarded,0xa5,sizeof(guarded));
    int result=modbus_on_frame(req,sizeof(req),guarded+16u,&length);
    unsigned size=0;
    if (exception) {
        if (addr) { expected[0]=addr; expected[1]=0x83; expected[2]=exception; size=3u; }
    } else {
        expected[0]=addr; expected[1]=3u; expected[2]=(u8)(qty*2u); size=3u+qty*2u;
        for (unsigned i=0; i<qty; ++i) {
            expected[3u+i*2u]=(u8)(words[i]>>8); expected[4u+i*2u]=(u8)words[i];
        }
    }
    if (size) {
        crc=wire_crc(expected,size); expected[size++]=(u8)crc; expected[size++]=(u8)(crc>>8);
    }
    if (result!=(addr!=0) || length!=size || memcmp(guarded+16u,expected,size)) {
        fprintf(stderr,"Modbus failure addr=%u reg=%04x qty=%u exception=%u expected_len=%u actual_len=%lu result=%d\n",
                addr,reg,qty,exception,size,(unsigned long)length,result);
        abort();
    }
    for (unsigned i=0; i<16u; ++i) {
        assert(guarded[i]==0xa5); assert(guarded[16u+MODBUS_RTU_FRAME_CAPACITY+i]==0xa5);
    }
    for (unsigned i=0; i<size; ++i) { corpus^=expected[i]; corpus*=16777619u; }
    ++frames;
}
static void expect_write_single(u8 addr,u16 reg,u16 value,u8 exception)
{
    u8 req[8]={addr,6u,(u8)(reg>>8),(u8)reg,(u8)(value>>8),(u8)value};
    u8 rsp[16]={0};u32 length=0u;u16 crc=wire_crc(req,6u);
    req[6]=(u8)crc;req[7]=(u8)(crc>>8);
    int result=modbus_on_frame(req,sizeof(req),rsp,&length);
    if(!addr){assert(!result&&length==0u);return;}
    assert(result);
    if(exception){assert(length==5u&&rsp[1]==0x86u&&rsp[2]==exception);}
    else{assert(length==8u&&!memcmp(req,rsp,6u));}
    assert(wire_crc(rsp,length)==0u);
}

static void check_manual_soc_write(void)
{
    u8 req[11] = {1u, 0x10u, 0x10u, 0x05u, 0u, 1u, 2u, 0u, 88u};
    u8 rsp[16];
    u32 length;
    u16 crc;

    expect_write_single(1u, BMS_PARAM_REG_SOC, 67u, 0u);
    expect_write_single(0u, BMS_PARAM_REG_SOC, 80u, 0u);
    assert(manual_soc_writes == 1u && manual_soc == 67u);

    crc = wire_crc(req, 9u);
    req[9] = (u8)crc; req[10] = (u8)(crc >> 8);
    assert(modbus_on_frame(req, sizeof(req), rsp, &length));
    assert(length == 8u && !memcmp(req, rsp, 6u) && wire_crc(rsp, length) == 0u);
    assert(manual_soc_writes == 2u && manual_soc == 88u);

    req[0] = 0u;
    crc = wire_crc(req, 9u);
    req[9] = (u8)crc; req[10] = (u8)(crc >> 8);
    assert(!modbus_on_frame(req, sizeof(req), rsp, &length) && length == 0u);
    assert(manual_soc_writes == 2u);
}

static void check_realtime_window(void)
{
    /* 固定协议期望独立于生产 switch：覆盖 D121 及全部合法起点/长度。 */
    u16 words[11]={0x4253u,1u,7643u,(u16)-123,67u,801u,611u,701u,3381u,3321u,60u};
    memset(&g_bms_report,0,sizeof(g_bms_report));
    g_bms_report.pack_voltage_10mv=7643u;
    g_bms_report.discharge_current_a10=123u;
    g_bms_report.soc.soc_percent=67u;
    g_bms_report.temperature_max_x10=801u;
    g_bms_report.temperature_min_x10=611u;
    g_bms_report.temperature_x10[MOS_TEMP1]=701u;
    g_bms_report.cell_max_mv=3381u;
    g_bms_report.cell_min_mv=3321u;
    g_bms_report.cell_delta_mv=60u;
    for (u16 start=0u;start<11u;++start)
        for (u16 count=1u;count<=11u-start;++count)
            expect_read(1u,(u16)(0xD120u+start),count,words+start,0u);
    g_bms_report.discharge_current_a10=0u;
    g_bms_report.charge_current_a10=45u;
    words[3]=45u;
    expect_read(1u,0xD120u,11u,words,0u);
    expect_read(1u,0xD11Fu,1u,words,2u);
    expect_read(1u,0xD12Au,2u,words,2u);
    expect_read(1u,0xD12Bu,1u,words,2u);
    memset(&g_bms_report,0,sizeof(g_bms_report));
}

static void check_product_identity(void)
{
    /* 独立构造原 3 个 32-byte 窗口；完整 SN、短 SN、空值和读取失败。 */
    u8 expected[96];
    u16 words[48];
    for (unsigned pass=0u; pass<4u; ++pass) {
        memset(&identity_user,0,sizeof(identity_user));
        identity_user_ok=pass!=3u;
        if (pass==1u) memcpy(identity_user.serial,"0123456789abcdefghijklmnopqrstuv",32u);
        if (pass>=2u) memcpy(identity_user.serial,"NEW-SN",7u);
        memset(expected,0,sizeof(expected));
        memcpy(expected,BMS_PRODUCT_DEFAULT_SERIAL,
               sizeof(BMS_PRODUCT_DEFAULT_SERIAL)>32u?32u:sizeof(BMS_PRODUCT_DEFAULT_SERIAL));
        if (identity_user_ok && identity_user.serial[0]) memcpy(expected,identity_user.serial,32u);
        memcpy(expected+32u,BMS_PRODUCT_HW_VERSION,
               sizeof(BMS_PRODUCT_HW_VERSION)>32u?32u:sizeof(BMS_PRODUCT_HW_VERSION));
        memcpy(expected+64u,BMS_PRODUCT_SW_VERSION,
               sizeof(BMS_PRODUCT_SW_VERSION)>32u?32u:sizeof(BMS_PRODUCT_SW_VERSION));
        for (unsigned i=0u; i<48u; ++i) words[i]=(u16)(((u16)expected[i*2u]<<8)|expected[i*2u+1u]);
        bms_product_info_refresh();
        expect_read(1u,PROD_SN_REG_BASE,48u,words,0u);
        expect_read(1u,PROD_HW_VER_REG_BASE,16u,words+16u,0u);
        expect_read(1u,PROD_SW_VER_REG_BASE,16u,words+32u,0u);
        expect_read(1u,PROD_SW_VER_REG_BASE+15u,2u,words,2u);
    }
    memset(&identity_user,0,sizeof(identity_user));identity_user_ok=1;
}

static void check_requested_profile(void)
{
    const u16 metadata[]={0x1234u,1u,
        DVC1124_DEFAULT_SHUNT_UOHM,BMS_PRODUCT_CELL_COUNT,DVC1124_I2C_WATCHDOG_SECONDS,
        1u,2u,3u,BMS_AFE_HW_INTERFACE_VERSION,
        DVC1124_DEFAULT_SHUNT_UOHM,1u,BMS_AFE_HW_FEATURE_DIAGNOSTIC|BMS_AFE_HW_FEATURE_RUNTIME_STATUS
    };
    u16 words[BMS_AFE_HW_REQUESTED_REG_COUNT];
    /* 包括数据、元数据及跨两者的全部子区间；RAM 校验失败整段数据未知。 */
    for (unsigned valid=0u; valid<2u; ++valid) {
        requested_valid=(u8)valid;
        for (unsigned start=0u; start<BMS_AFE_HW_REQUESTED_REG_COUNT; ++start)
            for (unsigned qty=1u; qty<=BMS_AFE_HW_REQUESTED_REG_COUNT-start; ++qty) {
                for (unsigned i=0u; i<qty; ++i) {
                    unsigned offset=start+i;
                    words[i]=offset<35u ? (valid?(u16)(requested_base+offset):0xffffu) : offset<47u ? metadata[offset-35u] :
                        offset<58u ? (u16)(0x3400u+offset-47u) : (u16)(0x3500u+offset-58u);
                    if (offset==36u) words[i]=(u16)valid;
                }
                requested_reads=effective_reads=0u;
                expect_read(1u,(u16)(BMS_AFE_HW_REQUESTED_REG_BASE+start),(u16)qty,words,0u);
                assert(requested_reads==(start<35u || (start<=36u && start+qty>36u)));
                assert(!effective_reads);
            }
    }
    requested_reads=0u;
    expect_read(1u,BMS_AFE_HW_REQUESTED_REG_BASE-1u,2u,words,2u);
    expect_read(1u,BMS_AFE_HW_REQUESTED_REG_BASE+BMS_AFE_HW_REQUESTED_REG_COUNT-1u,2u,words,2u);
    assert(!requested_reads);
    /* 下一帧重新取值；不跨请求复用失败结果或上次配置。 */
    for (unsigned pass=0u; pass<2u; ++pass) {
        requested_base=(u16)(0x1100u+pass*0x100u);
        for (unsigned i=0u; i<35u; ++i) words[i]=(u16)(requested_base+i);
        requested_reads=0u;
        expect_read(1u,BMS_AFE_HW_REQUESTED_REG_BASE,35u,words,0u);
        assert(requested_reads==1u);
    }
    requested_base=0x1000u;
}

int main(void)
{
    u16 words[125]={0};
    check_realtime_window();
    check_product_identity();
    check_requested_profile();
    check_manual_soc_write();
    for(unsigned i=0;i<16u;++i)words[i]=(u16)((0x2e00u+i)^0x55aau);
    parameter_reads=0u;expect_read(1u,0x2e00u,16u,words,0u);assert(parameter_reads==1u);
    /* 非法尾地址仍整帧拒绝；分派现在先读合法前缀，不再有独立的支持表预检。 */
    parameter_reads=0u;expect_read(1u,0x2e0fu,2u,words,2u);assert(parameter_reads==1u);
    /* 模拟相邻所有者：普通寄存器/参数/普通寄存器混读仍按原顺序编码。 */
    parameter_mixed=1u;
    memcpy(g_bms_report.mac_public,"\x10\x20\x30\x40\x50\x60",6u);
    words[0]=0x1020u;words[1]=1u^0x55aau;words[2]=0x5060u;
    expect_read(1u,0u,3u,words,0u);
    parameter_reads=0u;expect_read(1u,0u,4u,words,2u);assert(parameter_reads==1u);
    parameter_mixed=0u;memset(g_bms_report.mac_public,0,6u);
    /* 撤销的清事件命令和睡眠状态窗口均拒绝单写、块写及广播写。 */
    const u16 readonly_targets[]={0x1007u,BMS_SLEEP_STATUS_BASE,BMS_SLEEP_STATUS_END-1u};
    for(unsigned target=0;target<sizeof(readonly_targets)/sizeof(readonly_targets[0]);++target) {
    for (unsigned block=0; block<2; ++block) {
        for (unsigned broadcast=0; broadcast<2; ++broadcast) {
            u8 req[11]={broadcast?0u:1u,block?0x10u:0x06u,0x10u,0x07u,0x12u,0x34u};
            req[2]=(u8)(readonly_targets[target]>>8);req[3]=(u8)readonly_targets[target];
            u8 rsp[16]; u32 length=0u; unsigned n=6u;
            if (block) { req[4]=0u;req[5]=1u;req[6]=2u;req[7]=0x12u;req[8]=0x34u;n=9u; }
            u16 crc=wire_crc(req,n);req[n++]=(u8)crc;req[n++]=(u8)(crc>>8);
            assert(modbus_on_frame(req,n,rsp,&length)==!broadcast);
            if (broadcast) assert(length==0u);
            else {
                assert(length==5u && rsp[0]==1u && rsp[1]==(u8)(req[1]|0x80u) && rsp[2]==2u);
                crc=wire_crc(rsp,3u);assert(rsp[3]==(u8)crc && rsp[4]==(u8)(crc>>8));
            }
        }
    }
    }
    assert(addr_supported(0xD000)); assert(addr_supported(0xD03E));
    assert(!addr_supported(0xD03F)); assert(addr_supported(0x2140));
    assert(!addr_supported(0x2141)); assert(!addr_supported(0xFFFF));
    assert(addr_supported(BTNAME_REG_BASE));
    assert(addr_supported(BTNAME_REG_BASE+BTNAME_REG_COUNT-1));
    assert(!addr_supported(BTNAME_REG_BASE+BTNAME_REG_COUNT));
    assert(addr_supported(BMS_MOS_CONTROL_BASE));
    assert(addr_supported(BMS_MOS_CONTROL_BASE+BMS_MOS_CONTROL_COUNT-1u));
    assert(!addr_supported(BMS_MOS_CONTROL_BASE+BMS_MOS_CONTROL_COUNT));
    fet_driver=3u;fet_driver_valid=1u;
    { const u16 mos[]={BMS_MOS_CONTROL_MAGIC,1u,1u,1u,3u,1u,
        BMS_AFE_STATUS_OUTPUT_ENABLED|BMS_AFE_STATUS_SAMPLES_QUALIFIED,3u};
      expect_read(1u,BMS_MOS_CONTROL_BASE,8u,mos,0u); }
    expect_write_single(1u,BMS_MOS_CONTROL_BASE+2u,0u,0u);assert(remote_allowed_mask==2u);
    expect_write_single(1u,BMS_MOS_CONTROL_BASE+3u,0u,0u);assert(remote_allowed_mask==0u);
    expect_write_single(1u,BMS_MOS_CONTROL_BASE+2u,2u,3u);assert(remote_allowed_mask==0u);
    expect_write_single(1u,BMS_MOS_CONTROL_BASE+0u,1u,2u);assert(remote_allowed_mask==0u);
    expect_write_single(0u,BMS_MOS_CONTROL_BASE+2u,1u,0u);assert(remote_allowed_mask==0u);
    remote_allowed_mask=3u;
    /* 有效配置每个合法子区间只读一次完整 profile。 */
    {
        u16 base=BMS_AFE_HW_EFFECTIVE_REG_BASE;
        for (unsigned start=0; start<BMS_AFE_HW_PROFILE_WORD_COUNT; ++start)
            for (unsigned qty=1; qty<=BMS_AFE_HW_PROFILE_WORD_COUNT-start; ++qty) {
                for (unsigned i=0; i<qty; ++i) words[i]=(u16)(0x2000u+start+i);
                requested_reads=effective_reads=0;
                expect_read(1,(u16)(base+start),(u16)qty,words,0);
                assert(effective_reads==1u && requested_reads==0u);
            }
        {
            for (unsigned i=0; i<BMS_AFE_HW_PROFILE_WORD_COUNT; ++i)
                words[i]=0xffffu;
            requested_reads=effective_reads=0u;
            effective_fail_at=1;
            expect_read(1,base,BMS_AFE_HW_PROFILE_WORD_COUNT,words,0);
            assert(effective_reads==1u && requested_reads==0u);
        }
        effective_fail_at=-1;
    }
    /* 每个新请求重新回读，失败结果不能污染下一请求，也不能跨请求复用旧配置。 */
    for (unsigned i=0; i<7u; ++i) words[i]=0xffffu;
    effective_fail_at=1; effective_reads=0u;
    expect_read(1,BMS_AFE_HW_EFFECTIVE_REG_BASE+5u,7u,words,0);
    assert(effective_reads==1u);
    effective_fail_at=-1;
    for (unsigned pass=0; pass<2u; ++pass) {
        effective_base=(u16)(0x2100u+pass*0x100u);
        for (unsigned i=0; i<BMS_AFE_HW_PROFILE_WORD_COUNT; ++i) words[i]=(u16)(effective_base+i);
        effective_reads=0u;
        expect_read(1,BMS_AFE_HW_EFFECTIVE_REG_BASE,BMS_AFE_HW_EFFECTIVE_REG_COUNT,words,0);
        assert(effective_reads==1u);
    }
    effective_base=0x2000u;
    effective_reads=0u;
    expect_read(1,BMS_AFE_HW_EFFECTIVE_REG_BASE+BMS_AFE_HW_EFFECTIVE_REG_COUNT-1u,2u,words,2u);
    expect_read(1,BMS_AFE_HW_EFFECTIVE_REG_BASE-1u,2u,words,2u);
    assert(effective_reads==0u);
    words[0]=0x1234; words[1]=1;
    words[2]=DVC1124_DEFAULT_SHUNT_UOHM; words[4]=DVC1124_I2C_WATCHDOG_SECONDS;
    words[3]=BMS_PRODUCT_CELL_COUNT; words[5]=1; words[6]=2; words[7]=3; words[8]=BMS_AFE_HW_INTERFACE_VERSION;
    expect_read(1,BMS_AFE_HW_META_CAPABILITIES,9,words,0);
    for (unsigned broadcast=0; broadcast<2; ++broadcast) {
        u8 addr=broadcast?0u:1u;
        for (u16 qty=1; qty<=BMS_EVENT_LOG_REG_COUNT; ++qty) {
            for (u16 i=0; i<qty; ++i) words[i]=(u16)(0x4000u+i);
            event_reads=0;
            expect_read(addr,BMS_EVENT_LOG_REG_BASE,qty,words,0); assert(event_reads==qty);
        }
        expect_read(addr,BMS_EVENT_LOG_REG_BASE,BMS_EVENT_LOG_REG_COUNT+1u,words,2);
        words[0]=0x4447u; words[1]=1u; words[2]=0x0040u;
        expect_read(addr,0x2A00u,3u,words,0u);
        expect_read(addr,0x2A00u,4u,words,2u);
        expect_read(addr,0x2A03u,1u,words,2u);
        expect_read(addr,0x2900u,1u,words,2u);
        expect_read(addr,0xffff,2,words,2); expect_read(addr,0x2140,2,words,2);
        expect_read(addr,0x2100,0,words,3); expect_read(addr,0x2100,126,words,3);
    }
    assert(!addr_supported(0x2180)); expect_read(1,0x2180,1,words,2);
    printf("frames=%u corpus=%08lx identity/requested/effective/actual protocol checked\n",frames,(unsigned long)corpus);
    return 0;
}
