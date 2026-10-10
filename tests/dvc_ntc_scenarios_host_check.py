"""D008 NTC 有效性及三级压差→公共软件保护→R81 命令；不模拟物理 Gate。"""
import re
from validation_support import read, function, run_c, evidence

driver = read('bms/afe/dvc1124/dvc1124.c')
backend = read('bms/afe/dvc1124/dvc1124_bms.c')
feature = read('bms/afe/dvc1124/dvc1124_feature_backend.c')
constants = '\n'.join(re.findall(
    r'^#define DVC_(?:TEMP_TABLE_LEN|NTC_MIN_RES_OHM|NTC_MAX_RES_OHM)\s+[^\n]+',
    driver, re.M))
state = driver[driver.index('#define DVC_ADC_MAX_AGE_TICKS'):
               driver.index('static uint32_t s_snapshot_generation;')]
table_start = driver.index('static const uint16_t s_ntc_10k_table')
table = driver[table_start:driver.index('};', table_start) + 2]
production = constants + '\n' + table + '\n'
for signature in ('static uint16_t dvc_ntc_temp_report(',
                  'static uint8_t dvc_ntc_resistance(',
                  'void DVC1124_App_AFEGet('):
    production += function(driver, signature) + '\n'
for signature in ('static uint16_t dvc_get_configured_temperature(',
                  'static uint8_t dvc_configured_ntc_valid(',
                  'static uint8_t dvc_get_battery_temperature_range(',
                  'static void dvc_publish_temperature_report(',
                  'static uint8_t dvc_charge_blocked(',
                  'static uint8_t dvc_discharge_blocked(',
                  'static uint8_t dvc_set_fet_modes_if_changed(',
                  'static uint8_t dvc_apply_common_port_fet_state('):
    production += function(backend, signature) + '\n'
production += function(feature, 'static uint8_t dvc_ntc_valid(') + '\n'
production += function(feature, 'static uint16_t dvc_temp(') + '\n'
production += function(feature, 'uint8_t dvc1124_backend_get_feature_snapshot(') + '\n'
# 原始采集层不能独立清除公共 NTC 错误；公共保护统一读取两个电池探头和 MOS 探头。
assert 'BMS_ERROR_TEMP_BREAK' not in function(driver, 'void DVC1124_App_AFEGet(')
reset = function(driver, 'void DVC1124_AFE_Reset(')
assert 'memset(s_ntc_valid_samples, 0, sizeof(s_ntc_valid_samples))' in reset
code = read('tests/fixtures/dvc_ntc_scenarios.c').replace('/* PRODUCTION STATE */', state)
code = code.replace('/* PRODUCTION */', production)
out = run_c(code, sources=('bms/core/bms_state.c', 'bms/core/bms_sw_protection.c'),
            name='dvc-ntc-scenarios')
evidence({'domain': 'ntc_safety', 'product': 'd008', 'trace': out.strip(),
          'boundary': '真实采集/温度换算/有效性/仲裁函数体与完整公共保护、State；'
                      'RAM 寄存器替身不证明 ADC 精度、线路漏电或物理 Gate'})
