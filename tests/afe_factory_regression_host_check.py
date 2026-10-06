"""原厂保证范围/最小档位独立向量；保留旧参数读取，严格检查完整新写入。"""
import os
from validation_support import profile_prefix, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd014')
code = profile_prefix(product) + r'''
int main(void) {
 bms_afe_hw_profile_t p, old;
 bms_afe_hw_profile_build_default(&old);
 assert(bms_afe_hw_profile_validate(&old)); /* 已有参数不被导入校验静默重置。 */
 p=old;p.enable_mask=0;assert(bms_afe_hw_profile_validate_write(&p));
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
 /* RM V1.2 p18–20：最低250uV/4mV，延时200/8/4ms，200uOhm板级输入。 */
 p=old;p.enable_mask=BMS_AFE_HW_EN_OCD2;p.ocd_recover_a10=0;p.ocd2_a10=200;p.ocd2_delay_ms=4;
 assert(bms_afe_hw_profile_validate_write(&p));p.ocd2_a10=199;assert(!bms_afe_hw_profile_validate_write(&p));
 p.ocd2_a10=200;p.ocd2_delay_ms=3;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_OCD1;p.ocd_recover_a10=0;p.ocd1_a10=13;p.ocd1_delay_ms=8;
 assert(bms_afe_hw_profile_validate_write(&p));p.ocd1_a10=12;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_COV;p.cov_delay_ms=200;assert(bms_afe_hw_profile_validate_write(&p));
 p.cov_delay_ms=199;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_SC;p.sc_a10=1000;p.sc_delay_us=1992;
 assert(bms_afe_hw_profile_validate_write(&p));p.sc_delay_us=1993;assert(!bms_afe_hw_profile_validate_write(&p));
#else
 /* SH V1.0A/V0.2C p34/37/49，温度协议单位(C+40)*10。 */
 p=old;p.enable_mask=BMS_AFE_HW_EN_COV;p.cov_recover_mv=2990;
 p.cov_mv=3000;assert(bms_afe_hw_profile_validate_write(&p));p.cov_mv=4500;assert(bms_afe_hw_profile_validate_write(&p));
 p.cov_mv=4501;assert(!bms_afe_hw_profile_validate_write(&p));p.cov_mv=2999;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_CUV;p.cuv_recover_mv=3600;
 p.cuv_mv=1000;assert(bms_afe_hw_profile_validate_write(&p));p.cuv_mv=3500;assert(bms_afe_hw_profile_validate_write(&p));
 p.cuv_mv=999;assert(!bms_afe_hw_profile_validate_write(&p));p.cuv_mv=3501;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_OCD1|BMS_AFE_HW_EN_OCD2;p.ocd_recover_a10=0;
 p.ocd1_delay_ms=140;p.ocd2_delay_ms=25;
 p.ocd1_a10=(50000u+SH3673510_BOARD_SHUNT_UOHM-1u)/SH3673510_BOARD_SHUNT_UOHM;
 p.ocd2_a10=(100000u+SH3673510_BOARD_SHUNT_UOHM-1u)/SH3673510_BOARD_SHUNT_UOHM;
 assert(bms_afe_hw_profile_validate_write(&p));
 p.enable_mask=BMS_AFE_HW_EN_OCD2;assert(!bms_afe_hw_profile_validate_write(&p));
 p.enable_mask=BMS_AFE_HW_EN_OCD1|BMS_AFE_HW_EN_OCD2;--p.ocd2_a10;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_SC;p.ocd2_a10=1000;
#if SH3673510_BOARD_SHUNT_UOHM == 100u
 /* OCD2 10mV /100uOhm =100A；2..6倍。 */
 p.sc_a10=2000;assert(bms_afe_hw_profile_validate_write(&p));p.sc_a10=1999;assert(!bms_afe_hw_profile_validate_write(&p));
 p.sc_a10=6000;assert(bms_afe_hw_profile_validate_write(&p));p.sc_a10=6001;assert(!bms_afe_hw_profile_validate_write(&p));
 p.ocd2_a10=16000;p.sc_a10=65535;assert(!bms_afe_hw_profile_validate_write(&p)); /* 6倍将超过u16报告范围。 */
#else
 p.ocd2_a10=1;p.sc_a10=65535;assert(!bms_afe_hw_profile_validate_write(&p)); /* SC不能使用不可表示OCD2基准。 */
#endif
 p=old;p.enable_mask=BMS_AFE_HW_EN_COV;p.cov_delay_ms=139;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_CUV;p.cuv_delay_ms=489;assert(!bms_afe_hw_profile_validate_write(&p));
 p=old;p.enable_mask=BMS_AFE_HW_EN_TEMP;p.chg_ot_x10=800;p.chg_ot_recover_x10=750;
 p.dsg_ot_x10=850;p.dsg_ot_recover_x10=800;p.chg_ut_x10=200;p.chg_ut_recover_x10=250;p.dsg_ut_x10=0;p.dsg_ut_recover_x10=50;
 assert(bms_afe_hw_profile_validate_write(&p));p.chg_ot_x10=1101;assert(!bms_afe_hw_profile_validate_write(&p));
 p.chg_ot_x10=1100;assert(bms_afe_hw_profile_validate_write(&p));p.chg_ut_x10=199;assert(!bms_afe_hw_profile_validate_write(&p));
#endif
 puts("PASS 原厂范围/能力新写入边界，旧默认仍可读");return 0;
}
'''
output = run_c(code, name='afe-factory-range')
evidence({'domain':'afe_factory_range','product':product,'trace':output.strip(),
          'boundary':'真实默认/校验函数体；不签核默认值，不代表比较器实测'})
