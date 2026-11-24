"""原厂保证范围/最小档位独立向量；保留旧参数读取，严格检查完整新写入。"""
import os
from validation_support import profile_prefix, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd008')
code = profile_prefix(product) + r'''
int main(void) {
 bms_afe_hw_profile_t p, old;
 bms_afe_hw_profile_build_default(&old);
 assert(bms_afe_hw_profile_validate(&old)); /* 已有参数不被导入校验静默重置。 */
 p=old;p.enable_mask=0;assert(bms_afe_hw_profile_validate_write(&p));
 /* RM V1.2 p18–20：最低250uV/4mV，延时200/8/4ms，按实际编译分流器换算。 */
 p=old;p.enable_mask=BMS_AFE_HW_EN_OCD2;p.ocd_recover_a10=0;p.ocd2_a10=(uint16_t)((40000u+DVC1124_DEFAULT_SHUNT_UOHM-1u)/DVC1124_DEFAULT_SHUNT_UOHM);p.ocd2_delay_ms=4;
 assert(bms_afe_hw_profile_validate_write(&p));--p.ocd2_a10;assert(!bms_afe_hw_profile_validate_write(&p));
 p.ocd2_a10=(uint16_t)((40000u+DVC1124_DEFAULT_SHUNT_UOHM-1u)/DVC1124_DEFAULT_SHUNT_UOHM);p.ocd2_delay_ms=3;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_OCD1;p.ocd_recover_a10=0;p.ocd1_a10=(uint16_t)((2500u+DVC1124_DEFAULT_SHUNT_UOHM-1u)/DVC1124_DEFAULT_SHUNT_UOHM);p.ocd1_delay_ms=8;
 assert(bms_afe_hw_profile_validate_write(&p));--p.ocd1_a10;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_COV;p.cov_delay_ms=200;assert(bms_afe_hw_profile_validate_write(&p));
 p.cov_delay_ms=199;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_SC;p.sc_a10=1000;p.sc_delay_us=1992;
 assert(bms_afe_hw_profile_validate_write(&p));p.sc_delay_us=1993;assert(!bms_afe_hw_profile_validate_write(&p));
 puts("PASS 原厂范围/能力新写入边界，旧默认仍可读");return 0;
}
'''
output = run_c(code, name='afe-factory-range')
evidence({'domain':'afe_factory_range','product':product,'trace':output.strip(),
          'boundary':'真实默认/校验函数体；不签核默认值，不代表比较器实测'})
