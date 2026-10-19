"""真实 DVC 默认构造、寄存器编码/验证与逐访问持久总线故障。"""
from validation_support import read, profile_prefix, function, run_c, evidence
from validation_support import ROOT
import sys,re
sys.path.insert(0,str(ROOT/'bms_tools'))
from configure_parameters import Configuration
from afe_source_fields import SourceProtectionFields

source=read('bms/afe/dvc1124/dvc1124.c')
functions='\n'.join(function(source,signature) for signature in (
    'static uint8_t dvc_write_verified_block(', 'static uint8_t dvc_write_verified(',
    'static uint8_t dvc_update_reg(', 'static uint8_t dvc_voltage_delay_code(',
    'static uint8_t dvc_linear_delay_code(',
    'static uint8_t dvc_current_to_oc1_code(', 'static uint8_t dvc_current_to_oc2_code(',
    'static uint8_t dvc_apply_protection_from_params(',
    'static uint8_t DVC1124_SetShortCircuitProtection('))
code=profile_prefix('d008')+'\n#include "dvc1124.h"\n'
code+='static uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us);\n'
code+=read('tests/fixtures/dvc_register_scenarios.c').replace('/* DRIVER */',functions)
traces={}
for profile in (1,2,3):
    raw=run_c(code,flags=[f'-DD008_PRODUCT_PROFILE={profile}'],name='dvc-registers').strip()
    traces[{1:'24s-lfp',2:'20s-nmc',3:'16s-lfp'}[profile]]=raw
    config=Configuration('d008','nmc' if profile==2 else 'lfp');codec=SourceProtectionFields(config)
    actual={int(a,16):int(v,16) for a,v in re.findall(r'([0-9a-f]{2}):([0-9a-f]{2});',raw)}
    for address,value in codec.registers().items():
        register=next(r for r in config.family['registers'] if r['address']==address)
        mask=register['write_mask']
        assert value&mask==actual[address]&mask,(profile,address,value,actual[address])
print('PASS 源码窗口预计保护编码与真实 DVC 写入逐字段一致')
evidence({'domain':'afe_registers','product':'d008','profiles':traces,
          'boundary':'真实 builder/validator/寄存器编码函数与 RAM I2C；不含总线电气、ADC 精度和物理 MOS'})
