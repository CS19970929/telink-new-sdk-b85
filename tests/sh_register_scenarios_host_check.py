"""真实默认构造 + 完整 SH control 函数体；只模拟寄存器总线和 GPIO。"""
import os
import re
from validation_support import read, profile_prefix, without_includes, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd014')
assert product in ('d011', 'd013', 'd014')
prefix = profile_prefix(product)
prefix += '#include "sh3673520.h"\n#include "sh3673510_control.h"\n#include "sh3673510_ntc.h"\n'
# GPIO 引脚值在 host 仅为唯一键，不声称模拟 Telink 电气行为。
for port in 'ABCD':
    for pin in range(8):
        prefix += f'#define GPIO_P{port}{pin} {(ord(port)-65)*8+pin}\n'
prefix += read('tests/fixtures/sh_register_scenarios.c').replace(
    '/* CONTROL */', without_includes(read('bms/afe/sh3673510/sh3673510_control.c')))
raw = run_c(prefix, ['bms/afe/sh3673510/sh3673510_ntc.c'],
            flags=['-Wno-unused-parameter', '-Wno-unused-function'], name='sh-registers')
evidence({'domain': 'afe_registers', 'product': product, 'trace': raw.strip(),
          'boundary': '默认/量化/完整 control + RAM 寄存器；SPI CRC/时序与芯片模拟量未覆盖'})
