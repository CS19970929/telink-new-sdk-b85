/* RAM AFE 只承接总线读写；禁止在此复制产品量化/保护实现。 */
#include <stdlib.h>
typedef unsigned GPIO_PinTypeDef;
#define AS_GPIO 0
#define Level_High 1
#define Level_Low 0
static void gpio_set_func(unsigned p,unsigned v){}
static void gpio_set_output_en(unsigned p,unsigned v){}
static void gpio_set_input_en(unsigned p,unsigned v){}
static void gpio_write(unsigned p,unsigned v){}
static int gpio_read(unsigned p){return 0;}
static void cpu_set_gpio_wakeup(unsigned p,unsigned l,unsigned e){}
static uint8_t regs[256];
static unsigned operation, fail_at, corrupt_at, written[256], assertions;
static unsigned read_operations[256];
static unsigned previous_write;
static bms_afe_hw_profile_t requested;
static void check(unsigned actual,unsigned expected,const char *what)
{
    ++assertions;
    if(actual!=expected){fprintf(stderr,"SH expected=%u actual=%u field=%s bus_operation=%u fail_at=%u corrupt_at=%u\n",expected,actual,what,operation,fail_at,corrupt_at);exit(1);}
}
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p){*p=requested;return bms_afe_hw_profile_validate(p);}
sh3673520_status_t SH3673520_ReadReg(uint8_t r,uint8_t *v)
{
    ++operation;read_operations[operation]=(previous_write==r+1u);previous_write=0;
    if(operation==fail_at)return SH3673520_ERR_SPI;
    *v=regs[r];if(operation==corrupt_at)*v^=0xff;return SH3673520_OK;
}
sh3673520_status_t SH3673520_WriteReg(uint8_t r,uint8_t v)
{
    ++operation;previous_write=r+1u;if(operation==fail_at)return SH3673520_ERR_SPI;
    regs[r]=v;++written[r];return SH3673520_OK;
}
sh3673520_status_t SH3673520_ReadRegs(uint8_t r,uint8_t *v,sh3673520_size_t n)
{
    for(unsigned i=0;i<n;i++)if(SH3673520_ReadReg((uint8_t)(r+i),v+i)!=SH3673520_OK)return SH3673520_ERR_SPI;
    return SH3673520_OK;
}
sh3673520_status_t SH3673520_SetBalanceMask(uint32_t m,uint8_t n){return SH3673520_OK;}
sh3673520_status_t SH3673520_Init(void){return SH3673520_OK;}
sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t g){return SH3673520_PORT_OK;}
void sh3673520_port_delay_ms(uint32_t ms){}

/* CONTROL */

static void reset_bus(unsigned failure,unsigned corrupt)
{
    memset(regs,0,sizeof(regs));memset(written,0,sizeof(written));memset(read_operations,0,sizeof(read_operations));
    operation=0;fail_at=failure;corrupt_at=corrupt;previous_write=0;
}
static void decode(void)
{
    /* 独立解码使用寄存器地址/位与 64 位整数物理量，不调用生产量化函数。 */
    static const unsigned ov_delay[]={140,280,490,980,2030,3010,4970,10010};
    static const unsigned uv_delay[]={490,770,980,1470,2030,3010,4970,10010};
    sh3673510_protection_actual_t a;
    check(sh3673510_control_get_protection_actual(&a),1,"actual.valid");
    check(((regs[0x49]&3u)*256u+regs[0x4a])*5u,a.ov_mv,"OV 解码");
    check(((regs[0x4b]&3u)*256u+regs[0x4c])*5u,a.uv_mv,"UV 解码");
    check(ov_delay[(regs[0x49]>>4)&7],a.ov_delay_ms,"OV 延时");
    check(uv_delay[(regs[0x4b]>>4)&7],a.uv_delay_ms,"UV 延时");
    check((unsigned)((((uint64_t)(regs[0x4d]&15)+1)*5000*10+SH3673510_BOARD_SHUNT_UOHM-1)/SH3673510_BOARD_SHUNT_UOHM),a.ocd1_a10,"OCD1 解码");
    check((unsigned)((((uint64_t)(regs[0x4e]&15)+1)*10000*10+SH3673510_BOARD_SHUNT_UOHM-1)/SH3673510_BOARD_SHUNT_UOHM),a.ocd2_a10,"OCD2 解码");
    check((unsigned)((((uint64_t)(regs[0x50]&31)+1)*1375*10+SH3673510_BOARD_SHUNT_UOHM-1)/SH3673510_BOARD_SHUNT_UOHM),a.occ_a10,"OCC 解码");
    check(regs[0x43]&31u,SH3673510_BOARD_CELL_COUNT,"装配串数");
    check(regs[0x41]&3u,0,"初始化 MOS off");
    check(regs[0x45]&0xc0u,0,"TS3/TS4 不使用电池硬件阈值");
    check(a.ov_mv,((requested.cov_mv+2u)/5u)*5u,"requested/effective OV");
    check(a.uv_mv,((requested.cuv_mv+2u)/5u)*5u,"requested/effective UV");
    check(a.ocd1_a10>=requested.ocd1_a10,1,"OCD1 不向下量化");
    check(a.occ_a10>=requested.occ1_a10,1,"OCC 不向下量化");
    for(unsigned r=0x49;r<=0x54;r++)check(written[r]>0,1,"保护寄存器必须写入");
}
int main(void)
{
    bms_afe_hw_profile_build_default(&requested);
    check(bms_afe_hw_profile_validate(&requested),1,"产品默认校验");
    reset_bus(0,0);check(sh3673510_control_init(),1,"初始化");
    unsigned total=operation;decode();
    printf("registers=");for(unsigned r=0x40;r<=0x54;r++)printf("%02x:%02x;",r,regs[r]);puts("");
    /* 每一个配置总线访问返回失败均不得发布初始化成功。 */
    for(unsigned f=1;f<=total;f++){
        reset_bus(f,0);check(sh3673510_control_init(),0,"逐访问失败");
        check(sh3673510_control_ready(),0,"失败保持 not ready");
        check(sh3673510_control_set_fets(1,1),0,"初始化失败禁止开 MOS");
    }
    /* 每一个写后验证读返回错误值都必须拒绝；只注入实际读操作。 */
    reset_bus(0,0);check(sh3673510_control_init(),1,"重新初始化");
    operation=0;memset(read_operations,0,sizeof(read_operations));check(sh3673510_control_apply_protection(),1,"独立应用");
    unsigned apply_total=operation;
    unsigned apply_reads[256];memcpy(apply_reads,read_operations,sizeof(apply_reads));
    for(unsigned f=1;f<=apply_total;f++){
        reset_bus(0,0);check(sh3673510_control_init(),1,"恢复测试环境");
        operation=0;fail_at=f;
        check(sh3673510_control_apply_protection(),0,"配置应用逐访问失败");
        sh3673510_protection_actual_t a;check(sh3673510_control_get_protection_actual(&a),0,"失败不发布 effective");
    }
    for(unsigned f=1;f<=apply_total;f++)if(apply_reads[f]){
        reset_bus(0,0);check(sh3673510_control_init(),1,"恢复读回测试环境");
        operation=0;corrupt_at=f;
        check(sh3673510_control_apply_protection(),0,"配置写后读回不一致");
        sh3673510_protection_actual_t a;check(sh3673510_control_get_protection_actual(&a),0,"读回错误不发布 effective");
    }
    reset_bus(0,0);check(sh3673510_control_init(),1,"休眠前初始化");
    check(sh3673510_control_sleep(),1,"休眠");memset(regs,0,sizeof(regs));
    check(sh3673510_control_wake(),1,"唤醒重配");decode();
    printf("init_bus_operations=%u apply_bus_operations=%u assertions=%u\n",total,apply_total,assertions);
    return 0;
}
