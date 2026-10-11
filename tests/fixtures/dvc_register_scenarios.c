#include <stdlib.h>
static uint8_t regs[256];
static dvc1124_config_t s_cfg;
static bms_afe_hw_profile_t requested;
static unsigned operation,fail_from,corrupt_reg,checks;
static void check(unsigned actual,unsigned expected,const char *what)
{
    ++checks;if(actual!=expected){fprintf(stderr,"DVC field=%s expected=%u actual=%u bus_operation=%u fail_from=%u\n",what,expected,actual,operation,fail_from);exit(1);}
}
void DVC1124_GetConfig(dvc1124_config_t*c){*c=s_cfg;}
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t*p){*p=requested;return bms_afe_hw_profile_validate(p);}
static void dvc_delay_ms(uint16_t ms){(void)ms;}
uint8_t DVC1124_ReadRegisters(uint8_t r,uint8_t*d,uint8_t n)
{
    ++operation;if(fail_from && operation>=fail_from)return 0;
    memcpy(d,regs+r,n);if(corrupt_reg>=r&&corrupt_reg<(unsigned)r+n)d[corrupt_reg-r]^=0xff;return 1;
}
uint8_t DVC1124_WriteRegisters(uint8_t r,const uint8_t*d,uint8_t n)
{
    ++operation;if(fail_from && operation>=fail_from)return 0;
    memcpy(regs+r,d,n);return 1;
}
/* DRIVER */
static void reset(unsigned fail)
{
    memset(regs,0xa5,sizeof(regs));operation=0;fail_from=fail;corrupt_reg=256;
}
static void decode(void)
{
    static const unsigned delays[]={200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000};
    check((regs[0x70]*16u+(regs[0x71]>>4))+500u,requested.cov_mv,"COV mV");
    check(regs[0x72]*16u+(regs[0x73]>>4),requested.cuv_mv,"CUV mV");
    /* 固定默认配置的独立寄存器期望，来自基线轨迹及 RM V1.2 编码；不读取驱动私有缓存。 */
    check(delays[regs[0x71]&15u],1000u,"COV delay");
    check(delays[regs[0x73]&15u],1000u,"CUV delay");
    check(regs[0x59],8u,"OCD1 code");
    check(regs[0x5a],8u,"OCC1 code");
    check(regs[0x5e]&63u,0u,"OCD2 code");
    check(regs[0x5f]&63u,0u,"OCC2 code");
    check((regs[0x5b]+1u)*8u,96u,"OCD1 delay");
    check((regs[0x60]+1u)*4u,100u,"OCD2 delay");
    check(regs[0x5e]&0x80,0x80,"OC2 保留位");
    check(regs[0x62]&0x40,0x40,"产品已确认默认 SC 开启");
}
int main(void)
{
    memset(&s_cfg,0,sizeof(s_cfg));s_cfg.shunt_uohm=DVC1124_DEFAULT_SHUNT_UOHM;
    bms_afe_hw_profile_build_default(&requested);check(bms_afe_hw_profile_validate(&requested),1,"default");
    reset(0);check(dvc_apply_protection_from_params(),1,"apply");unsigned total=operation;decode();
    printf("registers=");for(unsigned r=0x59;r<=0x73;r++)printf("%02x:%02x;",r,regs[r]);puts("");
    for(unsigned f=1;f<=total;f++){reset(f);check(dvc_apply_protection_from_params(),0,"persistent bus failure");}
    for(unsigned r=0x70;r<=0x73;r++){reset(0);corrupt_reg=r;check(dvc_apply_protection_from_params(),0,"readback mismatch");}
    requested.enable_mask|=BMS_AFE_HW_EN_SC;requested.sc_a10=1000;requested.sc_delay_us=100;
    reset(0);check(dvc_apply_protection_from_params(),1,"enabled SC");
    check(regs[0x62]&0x40,0x40,"SC enable");check((regs[0x62]&63)*10u,20,"SC mV");
    check(regs[0x63],12,"SC delay code");
    printf("bus_operations=%u assertions=%u\n",total,checks);return 0;
}
