"""DVC 实读 profile：原 RM 编码、旧语义字段、分块失败和总线静默。"""
import os
from validation_support import read, profile_prefix, without_includes, function, run_c

os.environ['BMS_PRODUCT'] = 'd008'
source = without_includes(read('bms/afe/dvc1124/dvc1124_config_service.c'))
source = source[source.index('/* 将采样压差换算为 0.1 A 电流值。 */'):
                source.index('/* 读取请求的独立硬件保护参数。 */')]
source += '\n' + function(read('bms/core/bms_afe_hw_profile.c'),
                           'u8 bms_afe_hw_profile_get_effective(')
code = profile_prefix('d008') + without_includes(read('bms/afe/dvc1124/dvc1124_config_service.h')) + r'''
#include "dvc1124.h"
static u8 regs[256], allowed = 1u;
static unsigned calls, fail_at;
static u32 shunt;
static bms_afe_hw_profile_t requested;
u8 bms_afe_bus_access_allowed(void) { return allowed; }
void DVC1124_GetConfig(dvc1124_config_t *c) { memset(c,0,sizeof(*c)); c->shunt_uohm=shunt; }
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p) { *p=requested; return 1u; }
uint8_t DVC1124_ReadRegisters(uint8_t r,uint8_t *d,uint8_t n) {
    ++calls;
    assert((r>=0x59u && (unsigned)r+n<=0x5du) ||
           (r>=0x5eu && (unsigned)r+n<=0x64u) ||
           (r>=0x70u && (unsigned)r+n<=0x74u));
    if(calls==fail_at) return 0u;
    memcpy(d,regs+r,n); return 1u;
}
''' + source + r'''
/* 期望独立来自 RM V1.2 p18-20、23-24；不调用生产解码器。 */
static unsigned current(unsigned uv) {
    unsigned v=shunt?(uv*10u+shunt/2u)/shunt:0u;
    return v>65535u?65535u:v;
}
static void check(unsigned seed,unsigned sc) {
    static const unsigned delays[]={200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000};
    unsigned cov=(regs[0x70]*16u)+(regs[0x71]>>4);
    unsigned cuv=(regs[0x72]*16u)+(regs[0x73]>>4);
    unsigned expected[]={cov?cov+500u:0u,delays[regs[0x71]&15u],
        cuv,delays[regs[0x73]&15u],current(regs[0x59]*250u),(regs[0x5b]+1u)*8u,
        current(regs[0x5a]*250u),(regs[0x5c]+1u)*8u,
        regs[0x5e]&0x40u?current(((regs[0x5e]&63u)+1u)*4000u):0u,(regs[0x60]+1u)*4u,
        regs[0x5f]&0x40u?current(((regs[0x5f]&63u)+1u)*4000u):0u,(regs[0x61]+1u)*4u,
        regs[0x62]&0x40u?(regs[0x62]&63u)*10u:0u,(regs[0x63]*781u+50u)/100u};
    bms_afe_hw_profile_t p=requested, before;
    u16 *fields[]={&p.cov_mv,&p.cov_delay_ms,&p.cuv_mv,&p.cuv_delay_ms,
        &p.ocd1_a10,&p.ocd1_delay_ms,&p.occ1_a10,&p.occ1_delay_ms,
        &p.ocd2_a10,&p.ocd2_delay_ms,&p.occ2_a10,&p.occ2_delay_ms};
    (void)seed;
    calls=fail_at=0u; assert(DVC1124_ReadProtectionEffective(&p)); assert(calls==3u);
    for(unsigned i=0;i<12u;++i) assert(*fields[i]==expected[i]);
    assert(p.sc_a10==(sc?(u16)(expected[12]*10000u/DVC1124_DEFAULT_SHUNT_UOHM):requested.sc_a10));
    assert(p.sc_delay_us==(sc?expected[13]:requested.sc_delay_us));
    /* 全部 35 个字：未覆盖的恢复/温度/使能字段必须保持请求值。 */
    for(unsigned i=0;i<BMS_AFE_HW_PROFILE_WORD_COUNT;++i) {
        unsigned changed=0u;
        for(unsigned j=0;j<12u;++j) changed|=fields[j]==((u16*)&p+i);
        changed|=sc&&(((u16*)&p+i)==&p.sc_a10||((u16*)&p+i)==&p.sc_delay_us);
        if(!changed) assert(((u16*)&p)[i]==((u16*)&requested)[i]);
    }
    for(unsigned i=0;i<14u;++i) {
        u32 v=0xffffffffu;
        calls=fail_at=0u;
        assert(dvc_cfg_read_effective((dvc1124_config_field_t)(0x50u+i),&v)==DVC1124_CFG_OK);
        assert(v==expected[i] && calls==1u);
    }
    for(unsigned f=1u;f<=3u;++f) {
        p=requested; before=p; calls=0u; fail_at=f;
        assert(!DVC1124_ReadProtectionEffective(&p));
        assert(calls==f && !memcmp(&p,&before,sizeof(p)));
    }
    calls=fail_at=0u; allowed=0u; p=requested;
    assert(!DVC1124_ReadProtectionEffective(&p)); assert(!calls); allowed=1u;
    assert(bms_afe_hw_profile_get_effective(&p)); assert(calls==3u);
#if !DVC1124_HW_PROTECT_ENABLE
    assert(!p.enable_mask && !p.cov_mv && !p.cuv_mv && !p.ocd1_a10 && !p.occ1_a10 && !p.sc_a10);
#endif
}
int main(void) {
    const u32 shunts[]={0u,1u,DVC1124_DEFAULT_SHUNT_UOHM,667u};
    bms_afe_hw_profile_build_default(&requested);
    for(unsigned j=0;j<sizeof(shunts)/sizeof(shunts[0]);++j) {
        shunt=shunts[j];
        for(unsigned seed=0;seed<256u;++seed) {
            for(unsigned r=0;r<sizeof(regs);++r) regs[r]=(u8)(seed+r*17u);
            for(unsigned sc=0;sc<2u;++sc) {
                requested.enable_mask=(u16)(sc?BMS_AFE_HW_EN_SC:0u); check(seed,sc);
            }
        }
    }
    memset(regs,0,sizeof(regs)); shunt=DVC1124_DEFAULT_SHUNT_UOHM; check(0u,1u);
    calls=0u; assert(!DVC1124_ReadProtectionEffective(0)); assert(!calls);
    puts("PASS DVC effective: all byte codes, 4 shunts, 3 block faults, silence, 35-word preservation");
    return 0;
}
'''
for profile in (1, 2, 3):
    for hw in (0, 1):
        run_c(code, flags=[f'-DD008_PRODUCT_PROFILE={profile}', f'-DDVC1124_HW_PROTECT_ENABLE={hw}'],
              name='dvc-effective')
