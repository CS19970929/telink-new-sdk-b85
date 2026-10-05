#!/usr/bin/env python3
"""Execute production C with hardware/storage mocks; requires a host C compiler.

The SOC translation unit is included in full. PM/guard functions are extracted
unchanged because app.c depends on the target-only BLE SDK. These tests validate
software decisions, not SDK timing, electrical shutdown, or AFE silicon behavior.
"""
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
    text = selected_source(MOD / name) if name == 'app.c' else (MOD / name).read_text()
    tail = r"\s*\{" if signature.endswith(")") else r"[^;{}]*\)\s*\{"
    match = re.search(re.escape(signature) + tail, text)
    assert match, signature
    start = match.start()
    pos = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]


def main():
    parser = argparse.ArgumentParser(description="Run D008 production SOC/PM host checks")
    parser.add_argument('--trajectory', type=Path,
                        help='also write a 7-day production-C SOC trajectory CSV')
    parser.add_argument('--compile-soc-executable', type=Path,
                        help='compile the production-C replay executable and stop')
    parser.add_argument('--soc-only', action='store_true', help='仅运行公共 SOC 和所选产品 app 样本入口')
    args = parser.parse_args()
    floor = re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                      (MOD / 'conf.h').read_text(), re.M)
    assert floor is not None, "missing D008 current reliability floor"
    with tempfile.TemporaryDirectory(prefix='d008-host-') as directory:
        units = {
            'soc': ('#include "' + (ROOT/'bms/core/bms_soc_eta.c').as_posix() + '"\n') + source('bms_soc_defs.h') + '\n' + source('bms_diag.h') + '\n' + source('bms_soc.h') + '\n' +
                   source('bms_soc_profile.h') + '\n' +
                   'static int bms_config_store_set_soc(const bms_soc_config_t *c){if(!config_store_write_ok)return 0;stored_profile.battery_chemistry=c->chemistry;stored_profile.soc_profile_id=c->profile_id;return 1;}\n'
                   'static int bms_config_store_get_soc(bms_soc_config_t *c){bms_soc_get_default_config(c);c->chemistry=stored_profile.battery_chemistry;c->profile_id=stored_profile.soc_profile_id;return 1;}\n' + source('bms_soc.c') + '\n' + '\n'.join(re.findall(r'^#define SOC_LEARNING_(?:TEMP|CURRENT|PACK)_FAULT_MASK[^\n]*', (MOD/'app.c').read_text(), re.M)) + '\n' + function('app.c', 'static void app_update_soc_from_sample('),
        }
        if args.compile_soc_executable is None and not args.soc_only:
            units.update({
            'power': '\n'.join(function('app.c', sig) for sig in (
                'static uint8_t app_get_fresh_measurements(',
                'static int app_enter_power_off(', 'static void app_acc_sleep_hold(',
                'static int app_acc_sleep_requested(', 'static int app_enter_acc_sleep(', 'void blt_pm_proc(void)')),
            'guard': source('bms_afe_guard.c'),
            'current': function('dvc1124.c', 'static void dvc_publish_current_report('),
            })
        for name, code in units.items():
            if name == 'power':
                code = '#include "' + (ROOT/'bms/core/bms_debug_log.h').as_posix() + '"\n' + code
            fixture = (FIX / (name + '.c')).read_text()
            assert fixture.count('/* PRODUCTION_SOURCE */') == 1
            path = Path(directory) / (name + '.c')
            path.write_text(fixture.replace('/* PRODUCTION_SOURCE */', code)
                           .replace('/* CURRENT_FLOOR */', floor.group(0)))
            if args.compile_soc_executable is not None and name != 'soc':
                continue
            executable = (args.compile_soc_executable if
                          args.compile_soc_executable is not None else Path(directory) / name)
            executable.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-Wno-unused-parameter',
                str(path), *([*host_includes(ROOT),'-include',str(MOD/'bms_diag.h'),str(MOD/'bms_diag.c')] if name=='guard' else []), '-o', str(executable)], check=True)
            if args.compile_soc_executable is not None:
                print(f"WROTE production SOC replay executable: {executable}")
                return
            subprocess.run([str(executable)], check=True)
            if name == 'soc' and args.trajectory is not None:
                trajectory = subprocess.run([str(executable), '--trajectory'],
                                            check=True, text=True,
                                            stdout=subprocess.PIPE).stdout
                args.trajectory.parent.mkdir(parents=True, exist_ok=True)
                args.trajectory.write_text(trajectory)
                print(f"WROTE SOC trajectory: {args.trajectory}")


if __name__ == '__main__':
    main()
