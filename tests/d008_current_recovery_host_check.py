"""Execute the production D008 recovery and FET arbitration with fake I/O."""
from pathlib import Path
import os, subprocess, tempfile, re
ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
s = (MOD / 'dvc1124_bms.c').read_text(encoding='utf-8')
def function(name):
    start = s.index('static uint8_t ' + name + '(')
    end = s.index('\n}\n', start) + 3
    return s[start:end]
state = '#define DVC_BMS_SAMPLE_PERIOD_MS 200u\n' + s[s.index('#define DVC_OCC_RECOVERY_TICKS'):s.index('static uint16_t dvc_get_configured_temperature')]
fixture = (ROOT / 'tests/fixtures/d008_current_recovery.c').read_text()
code = fixture.replace('/* PRODUCTION */', state + function('dvc_recover_current_faults') + function('dvc_apply_common_port_fet_state'))
# Failed samples must break PB1 qualification, before returning from acquisition.
assert re.search(r'if \(!snapshot.valid\) \{\s*s_current_recovery.removed_pending = 0u;', s)
assert s.index('bms_sw_protection_update(&sw)') < s.index('alarm = dvc_recover_current_faults(&snapshot')
with tempfile.TemporaryDirectory(prefix='d008-current-') as tmp:
    c = Path(tmp) / 'check.c'; c.write_text(code)
    for hw in (0,1):
        exe = Path(tmp) / ('check%d.exe' % hw)
        subprocess.run([os.environ.get('CC','cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-DDVC1124_HW_PROTECT_ENABLE=%d' % hw,
                        '-I', str(MOD), str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('D008 current recovery HW=0/1: PASS')
