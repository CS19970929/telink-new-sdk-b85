"""完整生产 Modbus 模块、CRC/diag/logger；寄存器所有者用可控替身。"""
import os
from validation_support import ROOT, read, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd014')
output = run_c(read('tests/fixtures/modbus_read.c'),
               ['bms/core/' + name + '.c' for name in
                ('bms_crc', 'bms_state', 'bms_diag', 'bms_debug_log', 'bms_sw_protection')],
               ['-I', str(ROOT/'tests/fixtures/d014_safety_loop/include'),
                '-D__PROJECT_8258_BLE_SAMPLE__=1', '-DCHIP_TYPE=CHIP_TYPE_825x'],
               name='modbus-read')
evidence({'domain': 'protocol', 'observations': output.strip(),
          'boundary': '完整 modbus_rtu.c、真实 CRC/State/diag/logger；配置/事件/AFE 所有者替身，不含 UART/BLE/物理硬件'})
print('PASS production Modbus read frames, bounds, failure retries and SH mapping: ' + product)
