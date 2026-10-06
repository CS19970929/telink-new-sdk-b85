"""DVC1124-2 V1.2 固定配置语义：保留位、RC、读写验证和逐总线失败。"""
import os
from validation_support import function, read, run_c

os.environ['BMS_PRODUCT'] = 'd008'

source = read('bms/afe/dvc1124/dvc1124.c')
body = '\n'.join(function(source, 'uint8_t ' + name + '(') for name in (
    'DVC1124_EncodeCurrentWake', 'DVC1124_EncodeBodyDiode',
    'DVC1124_EncodeI2cWatchdog', 'DVC1124_ApplyProjectOperatingConfig',
    'DVC1124_WriteRegisterSafe'))
special = read('bms/afe/dvc1124/dvc1124_special.c')
body += '\nstatic uint8_t s_core_ot_event_latched;\n' + '\n'.join(
    function(special, signature) for signature in (
        'static uint8_t dvc1124_read_core_ot_raw(',
        'uint8_t DVC1124_SetCoreOtThresholdCode('))

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dvc1124.h"
/* 原 RM p17/22/25-27：次序是当前初始化要求，位掩码独立于驱动 helper。 */
static const uint8_t order[] = {
    0x55,0x56,0x6D,0x6E,0x74,0x75,0x77,0x78,0x79,0x52,0x65,0x66,0x76,0x53,0x54
};
static const uint8_t masks[] = {
    0x8D,0x0F,0x3F,0xF3,0xFF,0xFF,0xE7,0x0F,0xFF,0x1F,0xFF,0xFF,0x7F,0xFF,0xFF
};
/* 4ms CC1/32ms唤醒，10V泵，VADC同步/1.54ms，GP1..4 NTC，GP5/6低侧FET。 */
static const uint8_t desired[] = {
    0x88,0x0F,0x28,0xC1,0x49,0x7F,
    DVC1124_HW_PROTECT_ENABLE ? 0xC4 : 0xC0,0,0xFF,16,0,
    DVC1124_HW_PROTECT_ENABLE ? 2 : 0,0,
    DVC1124_HW_PROTECT_ENABLE ? 0x50 : 0xFF,
    DVC1124_HW_PROTECT_ENABLE ? 0x78 : 0xFF
};
static uint8_t regs[256];
static struct {uint8_t reg,write,failed;} trace[45];
static unsigned operation,fail_at,corrupt_at;
static uint8_t initial;
static uint8_t access(uint8_t reg,uint8_t write){
    assert(operation<45);
    trace[operation].reg=reg;trace[operation].write=write;
    trace[operation].failed=(operation+1==fail_at);
    ++operation;return operation!=fail_at;
}
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *data,uint8_t n){
    assert(n==1);
    if(!access(reg,0))return 0;
    *data=regs[reg];
    if(reg==0x76)regs[reg]&=0x7F; /* COTF 读清，生产 helper 必须先保留事件。 */
    return 1;
}
uint8_t DVC1124_WriteRegisters(uint8_t reg,const uint8_t *data,uint8_t n){
    unsigned i;
    assert(n==1);
    for(i=0;i<sizeof(order)&&order[i]!=reg;++i){}
    assert(i<sizeof(order));
    if(reg==0x76)assert(*data==desired[i]); /* 禁止重发 RC 状态位。 */
    else assert(*data==(uint8_t)((initial&~masks[i])|desired[i]));
    if(!access(reg,1))return 0;
    regs[reg]=*data;
    if(operation==corrupt_at)regs[reg]^=1u; /* ACK成功但实际配置损坏。 */
    return 1;
}
'''
tail = r'''
static void check(unsigned seed,unsigned fail,unsigned corrupt){
    unsigned i,pos=0;
    initial=(uint8_t)seed;memset(regs,initial,sizeof(regs));
    operation=0;fail_at=fail;corrupt_at=corrupt;s_core_ot_event_latched=0;
    assert(DVC1124_ApplyProjectOperatingConfig()==(!fail&&!corrupt));
    for(i=0;i<sizeof(order);++i){
        assert(pos<operation&&trace[pos].reg==order[i]&&!trace[pos].write);
        if(trace[pos++].failed)continue;
        assert(pos<operation&&trace[pos].reg==order[i]&&trace[pos].write);
        if(trace[pos++].failed)continue;
        assert(pos<operation&&trace[pos].reg==order[i]&&!trace[pos].write);
        ++pos;
    }
    assert(pos==operation);
    if(!fail&&!corrupt){
        assert(operation==45);
        for(i=0;i<sizeof(order);++i){
            uint8_t expected=(order[i]==0x76)?desired[i]:(uint8_t)((initial&~masks[i])|desired[i]);
            assert(regs[order[i]]==expected);
        }
        assert(s_core_ot_event_latched==((initial&0x80)!=0));
    }
}
int main(void){
    for(unsigned seed=0;seed<256;++seed){
        for(unsigned fail=0;fail<=45;++fail)check(seed,fail,0);
        for(unsigned write=2;write<=44;write+=3)check(seed,0,write);
    }
    /* 通用 RMW 绝不访问 W0C、RC、只读或越界地址。 */
    const uint8_t forbidden[]={0x00,0x01,0x76,0x8F,0xFF};
    for(unsigned i=0;i<sizeof(forbidden);++i){
        operation=0;assert(!DVC1124_WriteRegisterSafe(forbidden[i],0xFF));assert(!operation);
    }
    puts("PASS DVC: 256 initial bytes, 45 access failures, 15 ACK/readback corruptions, RC latch, forbidden RMW");
    return 0;
}
'''

if __name__ == '__main__':
    for profile in (1, 2, 3):
        for hw in (0, 1):
            run_c(prefix + body + tail, flags=[f'-DD008_PRODUCT_PROFILE={profile}',
                  f'-DDVC1124_HW_PROTECT_ENABLE={hw}'], name='dvc-fixed-policy')
