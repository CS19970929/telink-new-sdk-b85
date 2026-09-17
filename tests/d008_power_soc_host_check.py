#!/usr/bin/env python3
"""Execute production C with hardware/storage mocks; requires a host C compiler.

The SOC translation unit is included in full. PM/guard functions are extracted
unchanged because app.c depends on the target-only BLE SDK. These tests validate
software decisions, not SDK timing, electrical shutdown, or AFE silicon behavior.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
FIX = ROOT / 'tests/fixtures/d008_power_soc'


def source(name):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)', '', (MOD / name).read_text(), flags=re.M)


def function(name, signature):
    text = (MOD / name).read_text()
    start = text.index(signature)
    pos = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]


def main():
    floor = re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                      (MOD / 'conf.h').read_text(), re.M)
    assert floor is not None, "missing D008 current reliability floor"
    with tempfile.TemporaryDirectory(prefix='d008-host-') as directory:
        for name, code in {
            'soc': source('bms_soc_defs.h') + '\n' + source('SocEnhance.h') + '\n' +
                   source('bms_soc_profile.h') + '\n' +
                   'static int bms_config_store_set_soc(const bms_soc_config_t *c){if(!config_store_write_ok)return 0;stored_profile.battery_chemistry=c->chemistry;stored_profile.soc_profile_id=c->profile_id;return 1;}\n'
                   'static int bms_config_store_get_soc(bms_soc_config_t *c){bms_soc_get_default_config(c);c->chemistry=stored_profile.battery_chemistry;c->profile_id=stored_profile.soc_profile_id;return 1;}\n' + source('SocEnhance.c'),
            'power': '\n'.join(function('app.c', sig) for sig in (
                'static uint8_t app_get_fresh_measurements(',
                'static int app_enter_power_off(', 'void blt_pm_proc(void)')),
            'guard': source('bms_afe_guard.c'),
            'current': function('dvc1124.c', 'static void dvc_publish_current_report('),
        }.items():
            fixture = (FIX / (name + '.c')).read_text()
            assert fixture.count('/* PRODUCTION_SOURCE */') == 1
            path = Path(directory) / (name + '.c')
            path.write_text(fixture.replace('/* PRODUCTION_SOURCE */', code)
                           .replace('/* CURRENT_FLOOR */', floor.group(0)))
            executable = Path(directory) / name
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-Wno-unused-parameter',
                str(path), *(['-I',str(MOD),'-include',str(MOD/'bms_diag.h'),str(MOD/'bms_diag.c')] if name=='guard' else []), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
