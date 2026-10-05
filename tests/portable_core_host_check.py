"""统一入口包含原 CMake 三套完整 TU 断言；CMake 构建本身仍由 CI 验证。"""
from validation_support import read, run_c

for fixture, units in (
    ('storage_record', ('storage_record',)),
    ('bms_core', ('bms_state','bms_sw_protection','bms_crc')),
    ('bms_soc_eta', ('bms_soc_eta',))):
    run_c(read('tests/'+fixture+'_host_test.c'), ['bms/core/'+u+'.c' for u in units], name=fixture)
