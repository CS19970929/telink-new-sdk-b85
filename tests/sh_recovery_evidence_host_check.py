"""Exercise the complete selected SH BMS unit through its public sample API.

The device/time boundary and independent software/feature policy are simulated;
this is not a physical MOS/load test. No private state is set by the fixture.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from project_paths import host_includes, selected_source
from validation_support import function

ROOT = Path(__file__).resolve().parents[1]
fixture = (ROOT / 'tests/fixtures/sh_recovery_evidence.c').read_text(encoding='utf8')
profile_header = (ROOT / 'bms/core/bms_afe_hw_profile.h').read_text(encoding='utf8')
profile_type = re.search(r'typedef struct\s*\{.*?\} bms_afe_hw_profile_t;', profile_header, re.S).group()
fixture = fixture.replace('/* PROFILE TYPE */', profile_type)
def run(defines=()):
    products = (os.environ['BMS_PRODUCT'],) if 'BMS_PRODUCT' in os.environ else ('d011', 'd013', 'd014')
    for product in products:
        if product not in ('d011', 'd013', 'd014'):
            raise ValueError('SH 回归不适用于 ' + product)
        source = selected_source(ROOT / 'bms/afe/sh3673510/sh3673510_bms.c', product)
        guard = selected_source(ROOT / 'bms/core/bms_afe_guard.c', product)
        code = fixture.replace('/* PRODUCTION */', source).replace('/* GUARD */', guard)
        control = selected_source(ROOT / 'bms/afe/sh3673510/sh3673510_control.c', product)
        code = code.replace('/* MODE CONTROL */', function(control, 'uint8_t sh3673510_control_set_load_detection('))
        for opt in ('-O2', '-Os'):
            with tempfile.TemporaryDirectory(prefix='sh-evidence-') as folder:
                c = Path(folder) / 'check.c'
                exe = Path(folder) / 'check.exe'
                c.write_text(code, encoding='utf8')
                subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [opt, '-std=c99',
                    '-Wall', '-Wextra', '-Werror', *defines, *host_includes(ROOT, product),
                    str(c), '-o', str(exe)], check=True)
                subprocess.run([str(exe)], check=True)
        print('PASS ' + product + ' public SH sample/recovery evidence O2/Os ' + str(defines))


if __name__ == '__main__':
    run()
    run(('-DGUARD_TEST=1',))
    run(('-DMODE_TEST=1',))
