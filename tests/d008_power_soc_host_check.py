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
    text = selected_source(MOD / name) if name in ('app.c', 'app_power.c') else (MOD / name).read_text()
    return extract_function(text, signature)


def protective_sleep_state():
    text = (MOD / 'app_power.c').read_text()
    start = text.index('#define APP_AFE_ERROR_SLEEP_SECONDS')
    end = text.index('static app_protective_sleep_t s_protective_sleep;', start)
    return text[start:end] + 'static app_protective_sleep_t s_protective_sleep;\n'


def main():
    parser = argparse.ArgumentParser(description="Run D008 production SOC/PM host checks")
    parser.add_argument('--trajectory', type=Path,
                        help='also write a 7-day production-C SOC trajectory CSV')
    parser.add_argument('--compile-soc-executable', type=Path,
                        help='compile the production-C replay executable and stop')
    parser.add_argument('--soc-only', action='store_true', help='仅运行公共 SOC 和所选产品 app 样本入口')
    args = parser.parse_args()
    floor = re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                      (MOD / 'bms_soc.h').read_text(), re.M)
    assert floor is not None, "missing D008 current reliability floor"
    with tempfile.TemporaryDirectory(prefix='d008-host-') as directory:
        units = {
            'soc': ('#include "' + (ROOT/'bms/core/bms_soc_eta.c').as_posix() + '"\n') + source('bms_soc_defs.h') + '\n' + source('bms_diag.h') + '\n' + source('bms_soc.h') + '\n' +
                   source('bms_soc_profile.h') + '\n' +
                   'static int bms_config_store_set_soc(const bms_soc_config_t *c){if(!config_store_write_ok)return 0;stored_profile.battery_chemistry=c->chemistry;stored_profile.soc_profile_id=c->profile_id;return 1;}\n'
                   'static int bms_config_store_get_soc(bms_soc_config_t *c){bms_soc_get_default_config(c);c->chemistry=stored_profile.battery_chemistry;c->profile_id=stored_profile.soc_profile_id;return 1;}\n' + source('bms_soc.c') + '\n' + '\n'.join(re.findall(r'^#define SOC_SAMPLE_(?:TEMP|CURRENT|PACK)_FAULT_MASK[^\n]*', (MOD/'app.c').read_text(), re.M)) + '\n' + function('app.c', 'static void app_update_soc_from_sample('),
        }
        if args.compile_soc_executable is None and not args.soc_only:
            units.update({
            'power': protective_sleep_state() + '\n'.join(function('app_power.c', sig) for sig in (
                'static uint8_t app_get_fresh_measurements(',
                'static u32 app_sleep_retry_ms(', 'static void app_publish_sleep(',
                'static u32 app_pm_elapsed_limit(',
                'static void app_protective_wakeup_pin(', 'static void app_protective_sleep_hold(',
                'static void app_enter_protective_sleep(', 'static u8 app_protective_sleep_poll(',
                'static void app_dvc_publish_sleep(',
                'static int app_enter_power_off(', 'static void app_acc_sleep_hold(',
                'static int app_acc_sleep_requested(', 'static int app_enter_acc_sleep(', 'void app_power_process(')),
            'guard': source('bms_afe_guard.c'),
            'current': function('dvc1124.c', 'static void dvc_publish_current_report('),
            })
        for name, code in units.items():
            if name == 'power':
                code = '#include "' + (ROOT/'bms/core/bms_debug_log.h').as_posix() + '"\n' + code
            fixture = (FIX / (name + '.c')).read_text()
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
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=c99', '-DBMS_HOST_TEST=1', '-Wall', '-Wextra', '-Werror',
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


def check_soc_openwire():
    fixture = (FIX / 'soc.c').read_text().split('int main(', 1)[0]
    fixture, count = re.subn(r'static void bms_features_get_status\([^)]*\)\n\{.*?\n\}',
                            'void bms_features_get_status(bms_features_status_t *s);',
                            fixture, flags=re.S)
    assert count == 1
    soc = (('#include "' + (ROOT/'bms/core/bms_soc_eta.c').as_posix() + '"\n') + source('bms_soc_defs.h') + source('bms_diag.h') +
           source('bms_soc.h') + source('bms_soc_profile.h') +
           'static int bms_config_store_set_soc(const bms_soc_config_t *c){return config_store_write_ok;}\n'
           'static int bms_config_store_get_soc(bms_soc_config_t *c){bms_soc_get_default_config(c);return 1;}\n' +
           source('bms_soc.c') + '\n' + '\n'.join(re.findall(r'^#define SOC_SAMPLE_(?:TEMP|CURRENT|PACK)_FAULT_MASK[^\n]*', (MOD / 'app.c').read_text(), re.M)) + '\n' + function('app.c', 'static void app_update_soc_from_sample('))
    fixture = fixture.replace('/* PRODUCTION_SOURCE */', soc)
    fixture += '\nvoid bms_diag_trace(uint16_t event, uint32_t a, uint32_t b) {(void)event;(void)a;(void)b;}\n'
    floor = re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                      (MOD / 'bms_soc.h').read_text(), re.M)
    fixture = fixture.replace('/* CURRENT_FLOOR */', floor.group(0))
    feature = (MOD / 'bms_features.c').read_text()
    feature_type = feature[feature.index('typedef struct'):feature.index('static bms_feature_state_t')]
    afe = (MOD / 'bms_afe.h').read_text()
    afe_types = '\n'.join(re.findall(
        r'typedef (?:struct|enum)\s*\{[^}]*\}\s*(?:bms_afe_diag_state_t|bms_afe_openwire_result_t);',
        afe, re.S))
    features_h = (MOD / 'bms_features.h').read_text()
    constants = '\n'.join(line for line in features_h.splitlines()
                          if line.startswith('#define BMS_OPENWIRE_') or
                          line.startswith('#define BMS_FEATURE_SERVICE_PERIOD_MS'))
    constants += '\n#define BMS_OPENWIRE_FIRST_IDLE_SAMPLES (BMS_OPENWIRE_FIRST_IDLE_MS / BMS_FEATURE_SERVICE_PERIOD_MS)\n'
    constants += '#define BMS_OPENWIRE_PERIOD_SAMPLES (BMS_OPENWIRE_PERIOD_MS / BMS_FEATURE_SERVICE_PERIOD_MS)\n'
    fixture += '\n#define BMS_AFE_FEATURE_MAX_CELLS 24u\n' + afe_types + '\n' + constants
    fixture += '\n#define BMS_AFE_BACKEND 1u\n' + feature_type
    fixture += (Path(__file__).parent / 'fixtures/d008_soc_openwire.c').read_text()
    for signature in ('static uint8_t openwire_eligible(', 'static uint32_t openwire_wait_remaining(',
                      'static void openwire_put32(', 'static void publish_openwire(', 'static void finish_openwire(',
                      'static void service_openwire(',
                      'void bms_features_service(', 'void bms_features_get_status('):
        fixture = fixture.replace('/* FEATURE_SOURCE */',
                                  function('bms_features.c', signature) + '\n/* FEATURE_SOURCE */')
    fixture = fixture.replace('/* FEATURE_SOURCE */', '')
    with tempfile.TemporaryDirectory(prefix='d008-soc-openwire-') as directory:
        path = Path(directory) / 'test.c'
        path.write_text(fixture)
        for trace_enabled in (0, 1):
            executable = Path(directory) / f'test-{trace_enabled}.exe'
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                '-Wno-unused-parameter', f'-DBMS_SOC_OCV_TRACE_ENABLE={trace_enabled}',
                str(path), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)



if __name__ == '__main__':
    main()
    if '--soc-only' not in os.sys.argv and '--compile-soc-executable' not in os.sys.argv:
        check_soc_openwire()
