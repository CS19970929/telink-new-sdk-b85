#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
/* Compile the whole production module, with no function extraction. */
#include "modbus_rtu.c"

bms_protection_params_t g_bms_protection_params;
bool deepsleep_en;
static unsigned requested_reads, effective_reads, event_reads, frames;
static int requested_fail_at = -1, effective_fail_at = -1;
static uint32_t corpus = 2166136261u;
uint32_t bms_diag_tick(void) { return 123u; }

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
{ return profile(p,&requested_reads,requested_fail_at,0x1000u); }
u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *p)
{ return profile(p,&effective_reads,effective_fail_at,0x2000u); }
u16 bms_afe_hw_profile_capabilities(void) { return 0x1234u; }
u16 bms_afe_hw_profile_apply_state(void) { return 2u; }
u16 bms_afe_hw_profile_last_error(void) { return 3u; }
u8 bms_afe_hw_access_is_active(void) { return 1u; }
u16 bms_event_log_read_reg(u16 r) { ++event_reads; return (u16)(0x4000u+r); }
const char *btname_get(void) { return "BT_TEST"; }
int bms_parameter_readable(u16 r) { return r>=0x2E00u && r<=0x2E0Fu; }
u16 bms_parameter_read(u16 r) { return (u16)(r^0x55aau); }
int bms_config_get_user(bms_user_params_t *u) { memset(u,0,sizeof(*u)); return 1; }
void bms_config_store_get_default_protect(bms_protection_params_t *p) { (void)p; abort(); }
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t *p) { (void)p; abort(); }
u8 bms_protection_params_commit(const bms_protection_params_t *p) { (void)p; abort(); }
u8 bms_protection_params_valid(void) { abort(); }
u8 bms_parameter_write(u16 r,u16 n,const u8 *p) { (void)r; (void)n; (void)p; abort(); }
int btname_modbus_on_write_holding(u16 r,u16 n,const u16 *p)
{ (void)r; (void)n; (void)p; abort(); }
bms_afe_hw_error_t bms_afe_hw_profile_commit_be(const u8 *p,u16 n)
{ (void)p; (void)n; abort(); }
int bms_afe_hw_access_modbus_on_frame(const u8 *q,u32 n,u8 *r,u32 *l)
{ (void)q; (void)n; (void)r; (void)l; abort(); }

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
static u8 actual_valid;
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *a)
{
    *a=(sh3673510_protection_actual_t){0};
    a->ov_mv=3650; a->uv_mv=2700; a->ocd1_a10=111; a->ocd2_a10=222; a->occ_a10=333;
    a->ov_delay_ms=100; a->uv_delay_ms=200; a->ocd1_delay_ms=300;
    a->ocd2_delay_ms=400; a->occ_delay_ms=500;
    return actual_valid;
}
#else
dvc1124_config_result_t DVC1124_ConfigServiceRead(dvc1124_config_field_t f,u32 *v)
{ *v=(u32)f; return DVC1124_CFG_OK; }
dvc1124_config_result_t DVC1124_ConfigServiceReadRaw(u8 r,u8 *v)
{ *v=r; return DVC1124_CFG_OK; }
dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t f,u32 v)
{ (void)f; (void)v; abort(); }
dvc1124_config_result_t DVC1124_ConfigServiceWriteRaw(u8 r,u8 v)
{ (void)r; (void)v; abort(); }
#endif

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

int main(void)
{
    u16 words[125]={0};
    bms_diag_init(); bms_debug_log_init();
    /* The canceled event-clear command must reject both single/block writes. */
    for (unsigned block=0; block<2; ++block) {
        for (unsigned broadcast=0; broadcast<2; ++broadcast) {
            u8 req[11]={broadcast?0u:1u,block?0x10u:0x06u,0x10u,0x07u,0x12u,0x34u};
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
    assert(read_address_supported(0xD000)); assert(read_address_supported(0xD03E));
    assert(!read_address_supported(0xD03F)); assert(read_address_supported(0x2140));
    assert(!read_address_supported(0x2141)); assert(!read_address_supported(0xFFFF));
    assert(read_address_supported(BTNAME_REG_BASE));
    assert(read_address_supported(BTNAME_REG_BASE+BTNAME_REG_COUNT-1));
    assert(!read_address_supported(BTNAME_REG_BASE+BTNAME_REG_COUNT));
    /* All legal profile subranges and each possible single-read failure. */
    for (unsigned window=0; window<2; ++window) {
        u16 base=window ? BMS_AFE_HW_EFFECTIVE_REG_BASE : BMS_AFE_HW_REQUESTED_REG_BASE;
        for (unsigned start=0; start<BMS_AFE_HW_PROFILE_WORD_COUNT; ++start)
            for (unsigned qty=1; qty<=BMS_AFE_HW_PROFILE_WORD_COUNT-start; ++qty) {
                for (unsigned i=0; i<qty; ++i) words[i]=(u16)((window?0x2000u:0x1000u)+start+i);
                requested_reads=effective_reads=0;
                expect_read(1,(u16)(base+start),(u16)qty,words,0);
                assert((window?effective_reads:requested_reads)==qty);
            }
        for (unsigned fail=1; fail<=BMS_AFE_HW_PROFILE_WORD_COUNT; ++fail) {
            for (unsigned i=0; i<BMS_AFE_HW_PROFILE_WORD_COUNT; ++i)
                words[i]=(u16)((window?0x2000u:0x1000u)+i);
            words[fail-1]=0xffff; requested_reads=effective_reads=0;
            if (window) effective_fail_at=(int)fail; else requested_fail_at=(int)fail;
            expect_read(1,base,BMS_AFE_HW_PROFILE_WORD_COUNT,words,0);
            assert((window?effective_reads:requested_reads)==BMS_AFE_HW_PROFILE_WORD_COUNT);
        }
        requested_fail_at=effective_fail_at=-1;
    }
    words[0]=0x1234; words[1]=1;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    words[2]=DVC1124_DEFAULT_SHUNT_UOHM; words[4]=DVC1124_I2C_WATCHDOG_SECONDS;
#else
    words[2]=SH3673510_BOARD_SHUNT_UOHM; words[4]=SH3673510_BOARD_WDT_EN ? 32u : 0u;
#endif
    words[3]=SeriesNum; words[5]=1; words[6]=2; words[7]=3; words[8]=BMS_AFE_HW_INTERFACE_VERSION;
    expect_read(1,BMS_AFE_HW_META_CAPABILITIES,9,words,0);
    for (unsigned broadcast=0; broadcast<2; ++broadcast) {
        u8 addr=broadcast?0u:1u;
        for (u16 qty=1; qty<=BMS_EVENT_LOG_REG_COUNT; ++qty) {
            for (u16 i=0; i<qty; ++i) words[i]=(u16)(0x4000u+i);
            event_reads=0;
            expect_read(addr,BMS_EVENT_LOG_REG_BASE,qty,words,0); assert(event_reads==qty);
        }
        for (u16 qty=1; qty<=125; ++qty) {
            u8 bytes[250];
            assert(bms_diag_read(BMS_DIAG_BASE,qty,bytes));
            for (u16 i=0; i<qty; ++i) words[i]=(u16)(((u16)bytes[i*2u]<<8)|bytes[i*2u+1u]);
            expect_read(addr,BMS_DIAG_BASE,qty,words,0);
            assert(bms_debug_log_read(BMS_DEBUG_LOG_BASE,qty,bytes));
            for (u16 i=0; i<qty; ++i) words[i]=(u16)(((u16)bytes[i*2u]<<8)|bytes[i*2u+1u]);
            expect_read(addr,BMS_DEBUG_LOG_BASE,qty,words,0);
        }
        expect_read(addr,BMS_EVENT_LOG_REG_BASE,BMS_EVENT_LOG_REG_COUNT+1u,words,2);
        expect_read(addr,BMS_DIAG_BASE-1u,2,words,2);
        expect_read(addr,BMS_DIAG_END-1u,2,words,2);
        expect_read(addr,BMS_DEBUG_LOG_BASE-1u,2,words,2);
        expect_read(addr,BMS_DEBUG_LOG_END-1u,2,words,2);
        expect_read(addr,0xffff,2,words,2); expect_read(addr,0x2140,2,words,2);
        expect_read(addr,0x2100,0,words,3); expect_read(addr,0x2100,126,words,3);
    }
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    const u16 actual[]={1,3650,2700,111,222,333,100,200,300,400,500};
    assert(read_address_supported(0x2180)); assert(read_address_supported(0x218A));
    assert(!read_address_supported(0x218B));
    words[0]=0; for (unsigned i=1; i<11; ++i) words[i]=0xffff;
    expect_read(1,0x2180,11,words,0); actual_valid=1;
    expect_read(1,0x2180,11,actual,0);
#else
    assert(!read_address_supported(0x2180)); expect_read(1,0x2180,1,words,2);
#endif
    printf("frames=%u corpus=%08lx requested/effective retry order preserved\n",frames,(unsigned long)corpus);
    return 0;
}
