"""Execute production SH recovery/FET functions with deterministic fault injection.

No register behavior is emulated: the fixture supplies sampled flags and command
outcomes. This checks MCU policy, not SPI timing or physical FET conduction.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
source = (APP / 'sh3673510_bms.c').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'(?m)^(?:static )?(?:void|uint8_t|uint16_t) ' + name + r'\(', source)
    if match is None:
        raise AssertionError('production function missing: ' + name)
    end = source.index('\n}\n', match.start()) + 3
    return source[match.start():end]


state = source[source.index('#define SH3510_SAMPLE_MS'):source.index('/* Existing product 10K NTC table')]
functions = '\n'.join(function(name) for name in (
    'filter_samples', 'note_comm_error', 'charge_blocked', 'discharge_blocked',
    'sh3510_outputs_healthy', 'sh3510_apply_requested_fets', 'publish_hw_status',
    'service_short_recovery', 'hw_recovery_stable', 'service_afe_reconfiguration',
    'sh3673510_bms_afe_set_output_enabled', 'sh3673510_bms_afe_sleep',
))
fixture = (ROOT / 'tests/fixtures/sh3673510_recovery.c').read_text(encoding='utf-8')
code = fixture.replace('/* PRODUCTION */', state + functions)
failed_modes = []
with tempfile.TemporaryDirectory(prefix='sh3510-recovery-') as tmp:
    c = Path(tmp) / 'check.c'
    c.write_text(code, encoding='utf-8')
    for hw in (0, 1):
        exe = Path(tmp) / ('check%d.exe' % hw)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                        '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                        '-DSH3673510_HW_PROTECT_ENABLE=%d' % hw,
                        '-I', str(APP), str(c), '-o', str(exe)], check=True)
        if subprocess.run([str(exe)], check=False).returncode:
            failed_modes.append(hw)
if failed_modes:
    raise AssertionError('production recovery failures in HW modes: ' + str(failed_modes))
print('SH3673510 production recovery/FET fault injection HW=0/1: PASS')
