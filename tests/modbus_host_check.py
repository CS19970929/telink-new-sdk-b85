"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_modbus_address_host_check():
    print("CHECK modbus_address_host_check", flush=True)
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

def check_modbus_fuzz_host_check():
    print("CHECK modbus_fuzz_host_check", flush=True)
    """执行生产帧解析与 CRC，使用确定性畸形帧检查边界和副作用。"""
    from validation_support import read, function, run_c, evidence
    import re

    source = read('bms/core/modbus_rtu.c')
    parts = '\n'.join(function(source, sig) for sig in (
        'static int modbus_exception(', 'static u16 u16be(', 'static void put_u16be(',
        'int modbus_on_frame('))
    constants = '\n'.join(re.findall(r'^#define MB_\w+[^\n]*', source, re.M))
    constants += '\n'+'\n'.join(re.findall(r'^#define BMS_PARAM_REG_\w+[^\n]*', read('bms/core/bms_parameter_access.h'), re.M))
    constants += '\n' + re.search(r'^#define MODBUS_RTU_FRAME_CAPACITY[^\n]*', read('bms/core/modbus_rtu.h'), re.M).group(0)
    for header,names in (
        ('bms_afe_hw_access.h', ('BMS_AFE_HW_ACCESS_MODBUS_FUNC',)),
        ('bms_afe_hw_modbus.h', ('BMS_AFE_HW_REQUESTED_REG_BASE','BMS_AFE_HW_REQUESTED_REG_COUNT')),
        ('bms_event_log.h', ('BMS_EVENT_LOG_ENTRY_COUNT','BMS_EVENT_LOG_REG_BASE','BMS_EVENT_LOG_REG_COUNT')),
        ('btname_modbus.h', ('BTNAME_REG_BASE','BTNAME_REG_WORDS'))):
        for name in names:
            constants += '\n'+re.search(r'^#define '+name+r'\s+[^\n]*',read('bms/core/'+header),re.M).group()
    words=len(re.findall(r'    u16 (\w+);',read('bms/core/bms_afe_hw_profile.h')))
    assert words==35, 'profile 格式变化需要重新审查协议场景'
    constants += f'\n#define BMS_AFE_HW_PROFILE_WORD_COUNT {words}u\n'
    code = read('tests/fixtures/modbus_fuzz.c').replace('/* CONSTANTS */',constants).replace('/* PARSER */',parts)
    output = run_c(code, ['bms/core/bms_crc.c'], ['-Wno-unused-parameter'], 'modbus-fuzz')
    evidence({'domain': 'protocol', 'seed': 20261005, 'random_frames': 100000,
              'observations': output.strip(),
              'boundary': '真实 modbus_on_frame/CRC；寄存器存取为带计数替身；不含 UART IRQ、BLE 分片或 PHY'})

if __name__ == "__main__":
    check_modbus_address_host_check()
    check_modbus_fuzz_host_check()
