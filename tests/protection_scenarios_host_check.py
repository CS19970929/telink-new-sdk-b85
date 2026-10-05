"""在完整生产保护 TU 上运行边界序列与多故障组合，无算法替身。"""
from validation_support import read, run_c, evidence

output = run_c(read('tests/fixtures/protection_scenarios.c'),
               ['bms/core/bms_sw_protection.c', 'bms/core/bms_state.c'])
evidence({'domain': 'protection', 'boundary_groups': 12, 'levels': 3,
          'filter_values_10ms': [0, 1, 19, 20, 21, 40, 100, 65535],
          'fault_combinations': 1024, 'implementation': 'production_translation_units',
          'observations': output.strip()})
