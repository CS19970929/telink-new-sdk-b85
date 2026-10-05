"""真实 DVC 默认构造、寄存器编码/验证与逐访问持久总线故障。"""
from validation_support import read, profile_prefix, function, run_c, evidence

source=read('bms/afe/dvc1124/dvc1124.c')
state=source[source.index('/* Last values actually represented'):source.index('static uint8_t dvc_crc8')]
functions='\n'.join(function(source,signature) for signature in (
    'static uint8_t dvc_write_verified_block(', 'static uint8_t dvc_write_verified(',
    'static uint8_t dvc_update_reg(', 'static uint8_t dvc_voltage_delay_code(',
    'static uint8_t dvc_linear_delay_code(', 'static uint16_t dvc_current_from_sense_uv(',
    'static uint8_t dvc_current_to_oc1_code(', 'static uint8_t dvc_current_to_oc2_code(',
    'static void dvc_note_quant(', 'static uint8_t dvc_apply_protection_from_params(',
    'uint8_t DVC1124_SetShortCircuitProtection('))
code=profile_prefix('d008')+'\n#include "dvc1124.h"\n'
code+=read('tests/fixtures/dvc_register_scenarios.c').replace('/* DRIVER */',state+functions)
traces={}
for profile in (1,2,3):
    traces[{1:'24s-lfp',2:'20s-nmc',3:'16s-lfp'}[profile]]=run_c(code,flags=[f'-DD008_PRODUCT_PROFILE={profile}'],name='dvc-registers').strip()
evidence({'domain':'afe_registers','product':'d008','profiles':traces,
          'boundary':'真实 builder/validator/寄存器编码函数与 RAM I2C；不含总线电气、ADC 精度和物理 MOS'})
