"""Run the production SH sleep call chain with injected bus and PM failures.

Only peripheral outcomes are mocked. This proves software sequencing, not AFE
sleep current, GPIO timing, or physical MOS shutdown. No project temp files.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'


def function(file, name):
    source = (APP / file).read_text(encoding='utf-8')
    match = re.search(r'(?m)^(?:static )?(?:void|int|uint8_t|u32) ' + name + r'\(', source)
    if match is None:
        raise AssertionError('production function missing: ' + name)
    end = source.index('\n}\n', match.start()) + 3
    return source[match.start():end]


guard = (APP / 'bms_afe_guard.c').read_text(encoding='utf-8')
state = guard[guard.index('typedef struct'):guard.index('uint8_t bms_afe_bus_access_allowed')]
body = state + '\n'.join([
    function('sh3673510_control.c', 'sh3673510_control_sleep'),
    function('sh3673510_control.c', 'sh3673510_control_wake'),
    function('sh3673510_bms.c', 'sh3673510_bms_afe_sleep'),
    function('bms_afe_guard.c', 'inhibit_local'),
    function('bms_afe_guard.c', 'best_effort_shutdown'),
    function('bms_afe_guard.c', 'enter_failsafe_wait'),
    function('bms_afe_guard.c', 'note_invalid'),
    function('bms_afe_guard.c', 'bms_afe_sleep'),
    function('app.c', 'app_note_sleep_and_enter_deepsleep'),
    function('app.c', 'app_pm_elapsed_limit'),
])
app_source = (APP / 'app.c').read_text(encoding='utf-8')
for counter in ('sleep_cnt', 'sleep_veryvlow_cnt', 'sleep_vlow_cnt',
                'sleep_vnormal_cnt', 'afe_comm_err_sleepcnt'):
    assert f'{counter} = app_pm_elapsed_limit(' in app_source
    assert f'if (app_note_sleep_and_enter_deepsleep(1u)) {counter} = 0;' in app_source
fixture = (ROOT / 'tests/fixtures/sh3673510_sleep.c').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='sh3510-sleep-') as tmp:
    c, exe = Path(tmp) / 'check.c', Path(tmp) / 'check.exe'
    c.write_text(fixture.replace('/* PRODUCTION */', body), encoding='utf-8')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                    '-I', str(APP), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('SH production sleep/control/backend/guard/app fault injection: PASS')
