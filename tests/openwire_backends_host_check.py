"""SH 真实检测函数：两阶段资格、读清标志、超时、清理和原始掩码；不替代实板。"""
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
static uint8_t registers[256], fail_write;
bms_report_t g_bms_report;
uint32_t bms_diag_tick(void) { return tick; }
uint8_t SH3673520_IsReady(void) { return 1; }
uint8_t sh3673510_control_set_balance(uint16_t mask) { (void)mask; return 1; }
sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *out) {
    *out=registers[reg];
    if(reg==SH3673520_REG_FLAG3) registers[reg]=0; /* RC，检测为唯一读取者。 */
    return SH3673520_OK;
}
sh3673520_status_t SH3673520_ReadRegs(uint8_t reg,uint8_t *out,sh3673520_size_t count) {
    memcpy(out,registers+reg,count);return SH3673520_OK;
}
sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t value) {
    if(fail_write)return SH3673520_ERR_SPI;
    registers[reg]=value;return SH3673520_OK;
}
'''
code += without_includes(read('bms/afe/sh3673510/sh3673510_feature_backend.c'))
code += r'''
static void complete(uint8_t ind,uint32_t raw) {
    registers[SH3673520_REG_FLAG3]=SH3673520_FLAG3_OWD_FLG_MASK |
        (ind?SH3673520_FLAG3_OWD_IND_MASK:0);
    registers[SH3673520_REG_OWDH]=(uint8_t)(raw>>16);
    registers[SH3673520_REG_OWDH+1]=(uint8_t)(raw>>8);
    registers[SH3673520_REG_OWDH+2]=(uint8_t)raw;
}
int main(void) {
    bms_afe_openwire_result_t out;
    registers[SH3673520_REG_SCONF3]=SH3673520_SCONF3_CRLD_LOAD;
    complete(1,0xfffff);assert(sh3673510_backend_openwire_start()); /* 旧 FLAG 不授权新检测。 */
    assert(sh3673510_backend_openwire_poll(&out)==BMS_AFE_DIAG_BUSY && !out.phase_coverage);
    complete(0,1);assert(sh3673510_backend_openwire_poll(&out)==BMS_AFE_DIAG_BUSY);
    complete(1,2);assert(sh3673510_backend_openwire_poll(&out)==BMS_AFE_DIAG_READY);
    assert(out.valid && out.determinate && out.phase_coverage==3 && out.open_cell_mask==3);
    assert(out.raw_phase[0]==1 && out.raw_phase[1]==2);
    assert(sh3673510_backend_openwire_stop());
    assert(!(registers[SH3673520_REG_SCONF3]&SH3673520_SCONF3_OWD_EN_MASK));
    assert((registers[SH3673520_REG_SCONF3]&SH3673520_SCONF3_CRLD_EN_MASK)==SH3673520_SCONF3_CRLD_LOAD);
    assert(sh3673510_backend_openwire_start());complete(0,0);sh3673510_backend_openwire_poll(&out);
    complete(1,0);assert(sh3673510_backend_openwire_poll(&out)==BMS_AFE_DIAG_READY && out.valid && out.determinate && !out.open_cell_mask);
    assert(sh3673510_backend_openwire_stop());
    assert(sh3673510_backend_openwire_start());complete(0,2);sh3673510_backend_openwire_poll(&out);
    complete(1,0);assert(sh3673510_backend_openwire_poll(&out)==BMS_AFE_DIAG_READY && out.valid && !out.determinate && out.raw_phase[0]==2);
    assert(sh3673510_backend_openwire_stop()); /* 映射外的异常不能清旧故障或报告健康。 */
    tick=UINT32_MAX-32000u;assert(sh3673510_backend_openwire_start());tick+=2500u*32u;
    assert(sh3673510_backend_openwire_poll(&out)==BMS_AFE_DIAG_ERROR && out.error==BMS_OW_ERR_TIMEOUT);
    fail_write=1;assert(!sh3673510_backend_openwire_stop() && s_ow_busy);
    fail_write=0;assert(sh3673510_backend_openwire_stop() && !s_ow_busy);
    assert(sh3673510_backend_openwire_start());
    for(unsigned i=0;i<3;i++){complete(1,0);sh3673510_backend_openwire_poll(&out);}
    assert(out.error==BMS_OW_ERR_INCOMPLETE && out.phase_coverage==2);
    assert(sh3673510_backend_openwire_stop());
    puts("PASS SH OpenWire: stale RC, two-phase coverage/raw masks, healthy/fault/indeterminate, bounded timeout/tick wrap, verified cleanup");
    return 0;
}
'''
out = run_c(code, name='sh-openwire')
evidence({'domain': 'openwire', 'observations': out.strip(),
          'boundary': '真实 SH 检测函数；寄存器 peer 为替身，物理通道映射及 Gate/Vgs 未验证'})
