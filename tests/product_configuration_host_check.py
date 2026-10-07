"""运行真实默认构造/校验，导出四产品和 D008 三装配的全部保护参数。"""
import json
import os
import re
from validation_support import profile_prefix, read, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd014')
sw_fields = re.findall(r'uint16_t\s+(u16\w+)\s*;', read('bms/core/bms_protection_params.h'))
hw_fields = re.findall(r'    u16 (\w+);', read('bms/core/bms_afe_hw_profile.h'))
assert len(sw_fields) == 65 and len(hw_fields) == 35
code = profile_prefix(product) + '\n#include "bms_sw_protection.h"\n#include "bms_state.h"\n'
code += 'bms_protection_params_t g_bms_protection_params; uint8_t bms_protection_params_valid(void){return 1;}\n'
code += 'int main(void){bms_protection_params_t sw; bms_config_store_get_default_protect(&sw); bms_afe_hw_profile_t hw;\n'
code += 'bms_afe_hw_profile_build_default(&hw); assert(bms_afe_hw_profile_validate(&hw)); assert(bms_sw_protection_validate_params(&sw));\n'
code += 'printf("{\\"cells\\":%u,\\"capacity_0p1ah\\":%u,\\"sw\\":{",(unsigned)BMS_PRODUCT_CELL_COUNT,(unsigned)BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH);\n'
for index, name in enumerate(sw_fields):
    code += f'printf("{"," if index else ""}\\"{name}\\":%u",(unsigned)sw.{name});\n'
code += 'printf("},\\"afe_requested\\":{");\n'
for index, name in enumerate(hw_fields):
    code += f'printf("{"," if index else ""}\\"{name}\\":%u",(unsigned)hw.{name});\n'
code += 'puts("}}");return 0;}\n'
configs = {}
for profile in ((1, 2, 3) if product == 'd008' else (0,)):
    raw = run_c(code, ['bms/core/bms_sw_protection.c', 'bms/core/bms_state.c'],
                [f'-DD008_PRODUCT_PROFILE={profile}'] if profile else [], 'configuration')
    configs[{1:'24s-lfp',2:'20s-nmc',3:'16s-lfp'}.get(profile,'default')] = json.loads(raw)
evidence({'domain': 'configuration', 'product': product, 'profiles': configs,
          'boundary': '真实产品默认/校验；未模拟已保存设备参数、AFE 芯片精度或实际动作'})
