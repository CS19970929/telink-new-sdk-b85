"""SH 真实均衡函数：续期、回读、失败与 tick 回绕；不替代实板。"""
from validation_support import read, without_includes, profile_prefix, run_c, evidence
import os

code = profile_prefix(os.environ.get('BMS_PRODUCT', 'd011')) + r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_afe_driver.h"
#include "bms_state.h"
#include "bms_diag.h"
#include "sh3673510_project_config.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
static uint32_t tick;
static uint8_t registers[256], fail_write, fail_reads;
static unsigned balance_writes, balance_reads;
bms_report_t g_bms_report;
uint32_t bms_time_ms(void) { return tick; }
uint8_t sh3673510_control_set_balance(uint32_t mask) {
    ++balance_writes;
    if(fail_write)return 0;
    registers[SH3673520_REG_BALANCEH]=(uint8_t)(mask>>16);
    registers[SH3673520_REG_BALANCEH+1]=(uint8_t)(mask>>8);
    registers[SH3673520_REG_BALANCEH+2]=(uint8_t)mask;
    return 1;
}
sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *out) {
    *out=registers[reg];
    return SH3673520_OK;
}
sh3673520_status_t SH3673520_ReadRegs(uint8_t reg,uint8_t *out,sh3673520_size_t count) {
    if(reg==SH3673520_REG_BALANCEH) ++balance_reads;
    if(fail_reads){--fail_reads;return SH3673520_ERR_SPI;}
    memcpy(out,registers+reg,count);return SH3673520_OK;
}
sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t value) {
    if(fail_write)return SH3673520_ERR_SPI;
    registers[reg]=value;return SH3673520_OK;
}
'''
source = read('bms/afe/sh3673510/sh3673510_bms.c')
code += source[source.index('#if BMS_FEATURE_BALANCE_SUPPORTED\n#define SH_FEATURE_VALID_CELL_MASK'):]
code += r'''
#if BMS_FEATURE_BALANCE_SUPPORTED
int main(void) {
    tick=UINT32_MAX-1000u;
    assert(bms_afe_driver_set_balance_mask(1u, NULL)); assert(balance_writes==1u);
    for(unsigned i=0;i<200;i++)assert(bms_afe_driver_set_balance_mask(1u, NULL));
    assert(balance_writes==1u);
    tick+=20000u-1u;
    assert(bms_afe_driver_set_balance_mask(1u, NULL)); assert(balance_writes==1u);
    ++tick; assert(bms_afe_driver_set_balance_mask(1u, NULL)); assert(balance_writes==2u);
    tick+=20000u; fail_write=1;
    assert(!bms_afe_driver_set_balance_mask(1u, NULL));
    fail_write=0; assert(bms_afe_driver_set_balance_mask(1u, NULL)); assert(balance_writes==4u);
    registers[SH3673520_REG_BALANCEH+2]=0;
    assert(bms_afe_driver_set_balance_mask(1u, NULL)); assert(balance_writes==5u);
    balance_reads=0u;
    assert(bms_afe_driver_set_balance_mask(0u, NULL));
    assert(balance_writes==6u && balance_reads==2u); /* 非零 → 写零 → 回读确认 */
    balance_reads=0u;
    assert(bms_afe_driver_set_balance_mask(0u, NULL));
    assert(balance_writes==6u && balance_reads==1u); /* 原本为零只需一次有效回读 */
    /* 读取失败不能阻止最后一次OFF尝试，但没有确认就不能发布成功。 */
    registers[SH3673520_REG_BALANCEH+2]=1u;
    fail_reads=1u; balance_reads=0u;
    assert(bms_afe_driver_set_balance_mask(0u, NULL) && balance_writes==7u && balance_reads==2u);
    assert(!registers[SH3673520_REG_BALANCEH+2]);
    registers[SH3673520_REG_BALANCEH+2]=1u;
    tick++; fail_reads=2u;
    assert(!bms_afe_driver_set_balance_mask(0u, NULL) && balance_writes==8u);
    assert(s_balance_refresh_ms!=tick && !registers[SH3673520_REG_BALANCEH+2]);
    fail_reads=1u; fail_write=1u;
    assert(!bms_afe_driver_set_balance_mask(0u, NULL) && balance_writes==9u);
    fail_write=0u; fail_reads=1u;
    assert(!bms_afe_driver_set_balance_mask(1u, NULL) && balance_writes==9u);
    /* 同轮已验证实际值省去前置重复读，写入仍有唯一最终确认。 */
    uint32_t actual=123u;
    assert(bms_afe_driver_get_balance_mask(&actual) && actual==0u);
    balance_reads=0u;
    assert(bms_afe_driver_set_balance_mask(0u,&actual) && balance_reads==0u && balance_writes==9u);
    assert(bms_afe_driver_set_balance_mask(1u,&actual) && balance_reads==1u && balance_writes==10u);
    assert(bms_afe_driver_get_balance_mask(&actual) && actual==1u);
    balance_reads=0u;
    assert(bms_afe_driver_set_balance_mask(1u,&actual) && balance_reads==0u && balance_writes==10u);
    fail_reads=1u;
    assert(!bms_afe_driver_set_balance_mask(0u,&actual) && balance_writes==11u);
    assert(bms_afe_driver_set_balance_mask(0u,NULL)); /* 写后失败的旧值不能用于补偿。 */
    puts("PASS SH balance: refresh, tick wrap, failed write, readback and hardware timeout");
    return 0;
}
'''
code += '\n#else\nint main(void) { puts("PASS product disables balance; no backend operation compiled"); return 0; }\n#endif\n'
out = run_c(code, name='sh-balance')
evidence({'domain': 'balance', 'observations': out.strip(),
          'boundary': '真实 SH 均衡函数；寄存器 peer 为替身，物理均衡电流未验证'})
