"""D014 原始生产 TU 联合执行；替身仅限 SPI/Flash/GPIO/时钟端口。"""
from validation_support import ROOT, read, run_c, evidence
from pathlib import Path
import tempfile

SOURCES = [
    'bms/core/' + name + '.c' for name in (
        'bms_afe_guard', 'bms_afe_hw_access', 'bms_afe_hw_profile', 'bms_config_store',
        'bms_crc', 'bms_debug_log', 'bms_diag', 'bms_event_log', 'bms_soc', 'bms_soc_eta',
        'bms_state', 'bms_state_store', 'bms_sw_protection', 'bms_parameters', 'bms_soc_profile', 'bms_sleep_config', 'storage_record')
] + ['bms/app/bms_features.c', 'bms/platform/telink/bms_board.c'] + [
    'bms/afe/sh3673510/' + name + '.c' for name in (
        'sh3673510_bms', 'sh3673510_control', 'sh3673510_ntc',
        'sh3673520')
]
def main():
    compiled = set(read('bms/products/d014/sources.txt').splitlines())
    assert set(SOURCES) <= compiled, '联合场景必须使用产品实际编译的 TU'
    with tempfile.TemporaryDirectory(prefix='d014-loop-flash-') as directory:
        image = str(Path(directory)/'flash.dat')
        runs = [{'BMS_LOOP_CASE': case, 'BMS_LOOP_FLASH': image}
                for case in ('boot-failure', 'runtime', 'cold-reboot')]
        output = run_c(read('tests/fixtures/d014_safety_loop/loop.c'), SOURCES,
                       ['-I', str(ROOT/'tests/fixtures/d014_safety_loop/include')],
                       name='d014-safety-loop', runs=runs)
        transition = read('tests/fixtures/d014_safety_loop/loop.c').split('int main(void)')[0]
        transition += '#include "bms_update_policy.h"\n'
        transition += read('tests/fixtures/d014_safety_loop/chemistry_transition.c')
        includes = ['-I', str(ROOT/'tests/fixtures/d014_safety_loop/include'), '-Wno-unused-function']
        for name, chemistry, mask, identifier, scene in (
            ('seed-nmc', 'NMC', 0, 101, 'seed'),
            ('reject-partial-lfp', 'LFP', 4, 202, 'incomplete'),
            ('update-lfp', 'LFP', 15, 202, 'reload'),
            ('reboot-lfp', 'LFP', 15, 202, 'reload'),
            ('update-nmc', 'NMC', 15, 303, 'reload'),
            ('reboot-nmc', 'NMC', 15, 303, 'reload'),
        ):
            flags = includes + [f'-DBMS_PRODUCT_CHEMISTRY=BMS_SOC_CHEMISTRY_{chemistry}',
                f'-DBMS_OTA_UPDATE_MASK={mask}u', '-DBMS_OTA_UPDATE_ID_0=0u',
                f'-DBMS_OTA_UPDATE_ID_1={identifier}u', *[f'-DBMS_OTA_UPDATE_ID_{i}=0u' for i in range(2, 6)]]
            output += run_c(transition, SOURCES, flags, name='d014-chemistry-'+name,
                runs=[{'BMS_CHEM_SCENE':scene, 'BMS_LOOP_FLASH':image}])
    evidence({'domain': 'safety_chain', 'production_units': SOURCES,
              'observations': output.strip(),
              'boundary': '真实参数/存储记录/SW/feature/guard/SH 控制与驱动；RAM SPI/Flash 和时钟/GPIO，不代表物理 Gate 或 MCU 调度'})


if __name__ == '__main__':
    main()
