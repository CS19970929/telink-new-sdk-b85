"""SH 原厂 SCONF6 四通道位、旧35-word、未接入拒绝与完整control写回。"""
import os
from validation_support import read, profile_prefix, without_includes, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd014')
code = profile_prefix(product)
code += '#include "sh3673520.h"\n#include "sh3673510_control.h"\n#include "sh3673510_ntc.h"\n'
for port in 'ABCD':
    for pin in range(8): code += f'#define GPIO_P{port}{pin} {(ord(port)-65)*8+pin}\n'
fixture = read('tests/fixtures/sh_register_scenarios.c').split('int main(void)')[0]
code += fixture.replace('/* CONTROL */', without_includes(read('bms/afe/sh3673510/sh3673510_control.c')))
code += r'''
int main(void) {
    bms_afe_hw_profile_t original;
    sh3673510_protection_actual_t actual;
    bms_afe_hw_profile_build_default(&original);
    assert(sizeof(original)==70u);
    unsigned defaults=SH3673510_HW_DEFAULT_ENABLE_TEMP ?
        (SH3673510_HW_DEFAULT_ENABLE_TS1 | (SH3673510_HW_DEFAULT_ENABLE_TS2<<1) |
         (SH3673510_HW_DEFAULT_ENABLE_TS3<<2) | (SH3673510_HW_DEFAULT_ENABLE_TS4<<3)) : 0u;
    assert(bms_afe_hw_profile_temperature_channels(&original)==defaults);
    assert(original.enable_mask&BMS_AFE_HW_TEMP_CHANNEL_SELECT);
    /* 通道用例使用明确合法的共享温度，不把客户保留的历史 UTC=-25℃ 当作写入前提。 */
    original.chg_ot_x10=1050u;original.chg_ot_recover_x10=850u;
    original.dsg_ot_x10=1150u;original.dsg_ot_recover_x10=1050u;
    original.chg_ut_x10=200u;original.chg_ut_recover_x10=300u;
    original.dsg_ut_x10=0u;original.dsg_ut_recover_x10=100u;
    requested=original;requested.enable_mask=0xdfu; /* 既有设备的真实旧编码。 */
    reset_bus(0,0);assert(sh3673510_control_init());assert((regs[0x45]>>4)==3u);
    requested.enable_mask&=(uint16_t)~BMS_AFE_HW_EN_TEMP;
    assert(sh3673510_control_apply_protection());assert(!(regs[0x45]&0xf0u));
    for(unsigned channels=0;channels<16u;++channels) {
        requested=original;
        requested.enable_mask=(uint16_t)((original.enable_mask&0x7fu)|BMS_AFE_HW_TEMP_CHANNEL_SELECT|
            (channels<<8)|(channels?BMS_AFE_HW_EN_TEMP:0u));
        unsigned allowed=(bms_afe_hw_profile_capabilities()>>8)&15u;
        if(channels&~allowed){assert(!bms_afe_hw_profile_validate(&requested));continue;}
        assert(bms_afe_hw_profile_validate(&requested));
        operation=previous_write=0u;memset(read_operations,0,sizeof(read_operations));
        assert(sh3673510_control_apply_protection());
        unsigned expected_low=((original.enable_mask&BMS_AFE_HW_EN_COV)?1u:0u) |
            ((original.enable_mask&BMS_AFE_HW_EN_CUV)?2u:0u) |
            ((original.enable_mask&(BMS_AFE_HW_EN_OCD1|BMS_AFE_HW_EN_OCD2))?4u:0u) |
            ((original.enable_mask&BMS_AFE_HW_EN_SC)?8u:0u);
        assert((regs[0x45]>>4)==channels);assert((regs[0x45]&15u)==expected_low);
        assert(sh3673510_control_get_protection_actual(&actual));assert(actual.temperature_channels==channels);
    }
    requested=original;requested.enable_mask=BMS_AFE_HW_TEMP_CHANNEL_SELECT;
    requested.chg_ot_x10=0u;requested.chg_ut_x10=1450u;
    assert(bms_afe_hw_profile_validate_write(&requested)); /* 全关时不要求未启用阈值可量化。 */
    operation=previous_write=0u;assert(sh3673510_control_apply_protection());assert(!regs[0x45]);
    requested=original;requested.enable_mask=BMS_AFE_HW_TEMP_CHANNEL_SELECT|BMS_AFE_HW_EN_TS1;
    assert(!bms_afe_hw_profile_validate(&requested)); /* 总使能与通道不一致。 */
    requested.enable_mask=BMS_AFE_HW_EN_TEMP|BMS_AFE_HW_EN_TS1;
    assert(!bms_afe_hw_profile_validate(&requested)); /* 无 SELECT 的新通道位不得混入旧格式。 */
    puts("PASS SH legacy mask, all16 channel selections, unfitted rejection, 35-word size and verified SCONF6 bits");
    return 0;
}
'''
output = run_c(code, ['bms/afe/sh3673510/sh3673510_ntc.c'],
    flags=['-Wno-unused-parameter', '-Wno-unused-function'], name='sh-temperature-channels')
evidence({'domain': 'afe_temperature_channels', 'product': product, 'trace': output.strip(),
          'boundary': '真实profile/control与RAM寄存器；未执行实板温度、MOS、掉电或唤醒验收'})
