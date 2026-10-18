"""复位默认 SCD 锁存→真实基础配置→正式 SCD 参数；不模拟比较器或物理 Gate。"""
import os
from validation_support import read, function, run_c, evidence

os.environ['BMS_PRODUCT'] = 'd008'
source = read('bms/afe/dvc1124/dvc1124.c')
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dvc1124.h"
#include "bms_diag.h"
static uint8_t regs[256], stuck_fet, hold_scd;
static unsigned operations, fail_from, sc_disables;
static void dvc_delay_ms(uint16_t ms) { (void)ms; }
uint8_t DVC1124_ReadRegisters(uint8_t r, uint8_t *p, uint8_t n) {
    if (++operations >= fail_from && fail_from) return 0;
    memcpy(p, regs+r, n); return 1;
}
uint8_t DVC1124_WriteRegisters(uint8_t r, const uint8_t *p, uint8_t n) {
    if (++operations >= fail_from && fail_from) return 0;
    if (r == 0x00) { regs[0] &= p[0]; return 1; }
    if (r == 0x62 && !(p[0] & 0x40)) {
        /* DS V1.1 p19：SCDE=0解除SCD；必须先关闭全部功率命令并确认R6。 */
        assert(!(regs[0x51] & 0x6f) && !(regs[6] & 0x0f));
        ++sc_disables; if (!hold_scd) regs[0] &= (uint8_t)~1u;
    }
    memcpy(regs+r, p, n);
    if (r == 0x51 && !(p[0] & 0x6f) && !stuck_fet) regs[6] &= 0xf0;
    return 1;
}
static uint8_t dvc_apply_cell_masks(void) { return 1; }
static uint8_t DVC1124_SetMosState(uint8_t c, uint8_t d) {
    assert(!c && !d); regs[0x51] &= (uint8_t)~0x0f; return 1;
}
'''
for signature in (
    'static uint8_t dvc_write_verified_block(', 'static uint8_t dvc_write_verified(',
    'static uint8_t dvc_update_reg(',
    'uint8_t DVC1124_WriteRegisterSafe(',
    'static uint8_t DVC1124_EncodeCurrentWake(',
    'static uint8_t DVC1124_EncodeBodyDiode(',
    'static uint8_t DVC1124_EncodeI2cWatchdog(',
):
    code += '\n' + function(source, signature)
# 允许同一回归在修复前运行，证明原始启动路径确实保留了现场的SCD。
signature = 'static uint8_t dvc_prepare_startup_short_circuit('
if signature in source:
    code += '\n' + function(source, signature)
code += '\n' + function(source, 'static uint8_t dvc_apply_basic_config(')
code += '\n' + function(source, 'static uint8_t DVC1124_SetShortCircuitProtection(')
code += r'''
static uint8_t s_need_config;
static unsigned profile_applies, operating_applies;
static uint8_t operating_ok=1;
uint8_t DVC1124_ApplyProjectOperatingConfig(void) { ++operating_applies; return operating_ok; }
static uint8_t DVC1124_SetCellCount(uint8_t n) { (void)n; s_need_config=1; return 1; }
static uint8_t DVC1124_AFE_IsReady(void) { return 0; }
static uint8_t dvc_apply_protection_from_params(void) {
    ++profile_applies; return DVC1124_SetShortCircuitProtection(40,256);
}
void bms_diag_boot_word(uint16_t o, uint16_t v) { (void)o; (void)v; }
'''
code += '\n' + function(source, 'void DVC1124_UpdataAfeConfig(')
code += r'''
static void reset(void) {
    memset(regs, 0, sizeof(regs));
    regs[0] = 0x25; /* 现场SCD，加上其它锁存，禁止顺手清空。 */
    regs[0x62] = 0x40; regs[0x63] = 0;
    operations = fail_from = sc_disables = stuck_fet = hold_scd = 0;
    profile_applies=operating_applies=0; operating_ok=1; s_need_config=1;
}
int main(void) {
    reset(); assert(dvc_apply_basic_config());
    assert(DVC1124_SetShortCircuitProtection(40, 256));
    if (regs[0] != 0x24) {
        fprintf(stderr, "zero-discharge startup expected ALARM=0x24 actual=0x%02x\n", regs[0]);
        return 1;
    }
    assert(sc_disables == 1 && regs[0x62] == 0x44 && regs[0x63] == 0x20);
    /* 正式保护启用后新出现的SCD必须保留；不能在配置结束时无条件清告警。 */
    regs[0] |= 1; assert(DVC1124_SetShortCircuitProtection(40, 256));
    assert(regs[0] == 0x25 && sc_disables == 1);
    /* 所有预驱动/主驱动关闭、反馈确认之后才允许清除复位默认SCD。 */
    reset(); regs[0x51] = 0x6f; regs[6] = 0x0f;
    assert(dvc_apply_basic_config() && !regs[0x51] && !(regs[6]&15));
    for (unsigned flag=1; flag<=8; flag<<=1) {
        reset(); regs[6]=(uint8_t)flag; stuck_fet=1;
        assert(!dvc_apply_basic_config() && !sc_disables && regs[0]==0x25);
    }
    reset(); hold_scd=1; assert(!dvc_apply_basic_config() && regs[0]==0x25);
    reset(); assert(dvc_apply_basic_config()); unsigned total=operations;
    for (unsigned fail=1; fail<=total; ++fail) {
        reset(); fail_from=fail; assert(!dvc_apply_basic_config());
        assert(!(regs[0x51]&0x6f));
    }
    reset(); DVC1124_UpdataAfeConfig();
    assert(!s_need_config && profile_applies==1 && operating_applies==1 && regs[0]==0x24);
    reset(); operating_ok=0; DVC1124_UpdataAfeConfig();
    assert(s_need_config && profile_applies==1 && operating_applies==1);
    reset(); regs[6]=1; stuck_fet=1; DVC1124_UpdataAfeConfig();
    assert(s_need_config && !profile_applies && !sc_disables);
    reset(); hold_scd=1; DVC1124_UpdataAfeConfig();
    assert(s_need_config && !profile_applies);
    puts("PASS startup SCD cleanup, all FET feedback, new fault preservation, bus failure");
    return 0;
}
'''
out = run_c(code, flags=['-DBMS_SERVICE_DIAG_ENABLE=1'], name='dvc-startup-protection')
evidence({'product': 'd008', 'trace': out.strip(),
          'boundary': '真实基础配置、寄存器验证及SCD编码函数与RAM I2C模型；不证明比较器或物理Gate'})
