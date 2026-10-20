#!/usr/bin/env python3
"""Execute production C with hardware/storage mocks; requires a host C compiler.

The SOC translation unit is included in full. PM/guard functions are extracted
unchanged because app_power.c depends on the target-only BLE SDK. These tests validate
software decisions, not SDK timing, electrical shutdown, or AFE silicon behavior.
"""
from validation_support import function as extract_function
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import argparse
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOD = Sources(ROOT)
FIX = ROOT / 'tests/fixtures/d008_power_soc'


def source(name):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)', '', (MOD / name).read_text(), flags=re.M)


def function(name, signature):
    if signature == 'static int app_acc_sleep_requested(':
        text = (MOD / name).read_text()  # 保留策略宏以覆盖开启/关闭。
    else:
        text = selected_source(MOD / name, defines=('-DBMS_SERVICE_DIAG_ENABLE=1',)) if name in ('app.c', 'app_power.c') else (MOD / name).read_text()
    return extract_function(text, signature)


def protective_sleep_state():
    text = (MOD / 'app_power.c').read_text()
    start = text.index('#define APP_COMMAND_REPLY_MS')
    end = text.index('static app_protective_sleep_t s_protective_sleep;', start)
    policy = (MOD / 'bms_sleep_config.c').read_text()
    policy = policy[policy.index('static uint32_t sleep_elapsed_limit('):]
    return text[start:end] + 'static app_protective_sleep_t s_protective_sleep;\n' + policy


def main():
    parser = argparse.ArgumentParser(description="Run D008 production SOC/PM host checks")
    parser.add_argument('--trajectory', type=Path,
                        help='also write a 7-day production-C SOC trajectory CSV')
    parser.add_argument('--compile-soc-executable', type=Path,
                        help='compile the production-C replay executable and stop')
    parser.add_argument('--soc-only', action='store_true', help='仅运行公共 SOC 和所选产品 app 样本入口')
    parser.add_argument('--behavior-mode', choices=('off', 'service', 'board'),
                        help='以诊断关闭、服务诊断或独立板测宏运行相同业务轨迹')
    args = parser.parse_args()
    product = os.environ.get('BMS_PRODUCT', 'd008')
    product_parameters = '#define BMS_PRODUCT_ID ' + str(int(product[1:])) + 'u\n#include "' + (ROOT / 'bms/products' / product / (product + '_protection_config.h')).as_posix() + '"\n'
    floor = re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                      (MOD / 'bms_soc.h').read_text(), re.M)
    assert floor is not None, "missing D008 current reliability floor"
    with tempfile.TemporaryDirectory(prefix='d008-host-') as directory:
        units = {
            'soc': ('#include "' + (ROOT/'bms/core/bms_soc_eta.c').as_posix() + '"\n') + source('bms_soc_defs.h') + '\n' + product_parameters + source('bms_diag.h') + '\n' + source('bms_soc.h') + '\n' +
                   source('bms_soc_profile.h') + '\n' + source('bms_soc_profile.c') + '\n' +
                   'static bms_soc_profile_config_t committed_profile;\n'
                   'static uint16_t committed_generation;\n'
                   'uint16_t bms_config_soc_profile_generation(void){return committed_generation;}\n'
                   'const bms_soc_profile_config_t *bms_config_current_soc_profile(void){return config_store_read_ok && committed_generation?&committed_profile:0;}\n'
                   '#define BMS_PRODUCT_CHEMISTRY 1u\n' + source('bms_soc.c') + '\n' + '\n'.join(re.findall(r'^#define SOC_SAMPLE_(?:TEMP|CURRENT|PACK)_FAULT_MASK[^\n]*', (MOD/'app.c').read_text(), re.M)) + '\n' + function('app.c', 'static void app_update_soc_from_sample('),
        }
        if args.compile_soc_executable is None and not args.soc_only and args.behavior_mode is None:
            units.update({
            'power': protective_sleep_state() + '\n'.join(function('app_power.c', sig) for sig in (
                'static void app_protective_sleep_hold(',
                'static void app_prepare_deep_sleep(',
                'static void app_enter_protective_sleep(', 'static u8 app_command_sleep_poll(',
                'static u8 app_protective_sleep_poll(',
                'static void app_dvc_publish_sleep(',
                'static void app_acc_sleep_hold(',
                'static int app_acc_sleep_requested(', 'static int app_enter_acc_sleep(',
                'int app_power_before_suspend(', 'void app_power_process(',
                'uint8_t app_power_prepare_loop(')),
            'guard': source('bms_afe_guard.c') + '\n' + function('dvc1124_bms.c', 'void bms_afe_diag_poll('),
            'current': function('dvc1124.c', 'static void dvc_publish_current_report('),
            })
        for name, code in units.items():
            code = source('bms_timing.h') + '\n' + code
            if name == 'power':
                code = '#include "' + (ROOT/'bms/core/bms_debug_log.h').as_posix() + '"\n' + code
            fixture = (FIX / (name + '.c')).read_text()
            if name == 'soc' and args.behavior_mode is not None:
                # 共用外围替身；业务轨迹不读取诊断字段。关闭时不链接 ETA，检查无残留引用。
                fixture = fixture.split('/* PRODUCTION_SOURCE */')[0]
                fixture = fixture.replace('#define BMS_SERVICE_DIAG_ENABLE 1', '')
                fixture += '/* PRODUCTION_SOURCE */\n' + (FIX / 'soc_behavior.c').read_text()
                if args.behavior_mode == 'off':
                    code = re.sub(r'^#include "[^"\n]*/bms_soc_eta.c"\n', '', code, flags=re.M)
            if name == 'guard':
                policy = re.search(r'typedef struct\s*\{[^}]*\} bms_afe_driver_policy_t;', (ROOT/'bms/core/bms_afe_driver.h').read_text(encoding='utf-8'), re.S).group()
                fixture = policy + '\n' + fixture
            if name == 'power':
                fixture = '#include "' + (ROOT/'bms/core/bms_diag.h').as_posix() + '"\n' + fixture
            assert fixture.count('/* PRODUCTION_SOURCE */') == 1
            path = Path(directory) / (name + '.c')
            path.write_text(fixture.replace('/* PRODUCTION_SOURCE */', code)
                           .replace('/* CURRENT_FLOOR */', floor.group(0)))
            if args.compile_soc_executable is not None and name != 'soc':
                continue
            executable = (args.compile_soc_executable if
                          args.compile_soc_executable is not None else Path(directory) / name)
            executable.parent.mkdir(parents=True, exist_ok=True)
            diag_flags = ['-DBMS_SERVICE_DIAG_ENABLE=1']
            if args.behavior_mode is not None:
                diag_flags = ['-DBMS_SERVICE_DIAG_ENABLE=' + ('1' if args.behavior_mode == 'service' else '0'),
                              '-DBMS_SOC_BOARD_TEST_ENABLE=' + ('1' if args.behavior_mode == 'board' else '0')]
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-DBMS_PRODUCT_SWITCH_SLEEP_ENABLE=1',
                '-std=c99', *diag_flags, '-DBMS_HOST_TEST=1', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-Wno-unused-parameter',
                str(path), '-I', str(ROOT/'bms/core'), *([*host_includes(ROOT),'-include',str(MOD/'bms_diag.h'),str(MOD/'bms_diag.c')] if name=='guard' else []), '-o', str(executable)], check=True)
            if args.compile_soc_executable is not None:
                print(f"WROTE production SOC replay executable: {executable}")
                return
            if args.behavior_mode is not None:
                trajectory = subprocess.run([str(executable)], check=True, text=True,
                                            stdout=subprocess.PIPE).stdout
                if args.trajectory is not None:
                    args.trajectory.parent.mkdir(parents=True, exist_ok=True)
                    args.trajectory.write_text(trajectory)
                print(f"PASS SOC business trajectory: {args.behavior_mode}, {len(trajectory.splitlines())} rows")
                continue
            subprocess.run([str(executable)], check=True)
            if name == 'power':
                subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                    '-std=c99', '-DBMS_SERVICE_DIAG_ENABLE=1', '-DBMS_HOST_TEST=1', '-DBMS_PRODUCT_SWITCH_SLEEP_ENABLE=0',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                    str(path), '-I', str(ROOT/'bms/core'), '-o', str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
            if name == 'soc' and args.trajectory is not None:
                trajectory = subprocess.run([str(executable), '--trajectory'],
                                            check=True, text=True,
                                            stdout=subprocess.PIPE).stdout
                args.trajectory.parent.mkdir(parents=True, exist_ok=True)
                args.trajectory.write_text(trajectory)
                print(f"WROTE SOC trajectory: {args.trajectory}")
            if name == 'soc':
                trajectories = []
                for mode in ('off', 'service', 'board'):
                    output = (args.trajectory.with_name(args.trajectory.stem + '-' + mode + '.csv')
                              if args.trajectory is not None else Path(directory) / (mode + '.csv'))
                    subprocess.run([os.sys.executable, str(Path(__file__).resolve()),
                                    '--behavior-mode', mode, '--trajectory', str(output)], check=True)
                    trajectories.append(output.read_bytes())
                assert trajectories[0] == trajectories[1] == trajectories[2], 'diagnostic modes changed SOC business behavior'
                print('PASS SOC diagnostic modes: identical business outputs (off/service/board)')


if __name__ == '__main__':
    main()
