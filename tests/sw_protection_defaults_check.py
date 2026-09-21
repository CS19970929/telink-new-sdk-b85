#!/usr/bin/env python3
"""Execute common First/Second, Third and directional temperature filter boundaries."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--series-num', type=int, choices=(16, 20, 24), default=24)
    parser.add_argument('--boot-check', action='store_true',
                        help='Also execute Config/State/Event/LoadParam with RAM Flash')
    args = parser.parse_args()
    source = (MOD / 'bms_sw_protection.c').read_text(encoding='utf-8')
    filters = source[source.index('typedef struct'):source.index('static bms_sw_filter_t s_filter')]
    filters += '\n#define BMS_SW_PROTECTION_SAMPLE_MS 200u\n'
    filters += '\n'.join(function(source, signature) for signature in (
        'static uint16_t bms_sw_filter_samples(', 'static void bms_sw_filter_reset(',
        'static uint8_t bms_sw_filter_update(', 'static uint8_t bms_sw_temp_filter_update('))
    filter_tests = r'''
    bms_sw_filter_t f = {0};
    /* First/Second ignore Recover=900 while the high alarm is at 800. */
    assert(bms_sw_filter_update(&f, 850, 800, 900, 1, BMS_SW_HIGH, 0));
    for (int i=0; i<10; ++i)
        assert(bms_sw_filter_update(&f, 800, 800, 900, 1, BMS_SW_HIGH, 0));
    assert(!bms_sw_filter_update(&f, 799, 800, 900, 1, BMS_SW_HIGH, 0));
    assert(bms_sw_filter_update(&f, 430, 450, 430, 1, BMS_SW_LOW, 0));
    assert(bms_sw_filter_update(&f, 450, 450, 430, 1, BMS_SW_LOW, 0));
    assert(!bms_sw_filter_update(&f, 451, 450, 430, 1, BMS_SW_LOW, 0));
    /* Third retains hysteresis and recovery filtering after current disappears. */
    assert(!bms_sw_temp_filter_update(&f, 1, 950, 950, 900, 40, BMS_SW_HIGH, 1));
    assert(bms_sw_temp_filter_update(&f, 1, 950, 950, 900, 40, BMS_SW_HIGH, 1));
    assert(bms_sw_temp_filter_update(&f, 0, 925, 950, 900, 40, BMS_SW_HIGH, 1));
    assert(bms_sw_temp_filter_update(&f, 0, 900, 950, 900, 40, BMS_SW_HIGH, 1));
    assert(!bms_sw_temp_filter_update(&f, 0, 900, 950, 900, 40, BMS_SW_HIGH, 1));
    assert(!bms_sw_temp_filter_update(&f, 0, 950, 950, 900, 1, BMS_SW_HIGH, 1));
    assert(bms_sw_filter_update(&f, 400, 400, 430, 1, BMS_SW_LOW, 1));
    assert(bms_sw_filter_update(&f, 420, 400, 430, 1, BMS_SW_LOW, 1));
    assert(!bms_sw_filter_update(&f, 430, 400, 430, 1, BMS_SW_LOW, 1));
    /* Disabled threshold and uint16 endpoints require no +/-1 arithmetic. */
    assert(!bms_sw_filter_update(&f, 0, 0, 10, 1, BMS_SW_LOW, 0));
    assert(bms_sw_filter_update(&f, 65535, 65535, 65535, 1, BMS_SW_HIGH, 0));
    assert(!bms_sw_filter_update(&f, 65534, 65535, 65535, 1, BMS_SW_HIGH, 0));




    puts("PASS alarm boundaries, Third hysteresis/filter, temperature direction and disabled limits");
'''
    code = ('#include <stdint.h>\n#include <stdio.h>\n#include <assert.h>\ntypedef uint16_t u16;\n'
            + filters + '\nint main(void) {\n' + filter_tests + 'return 0;\n}\n')
    with tempfile.TemporaryDirectory(prefix='d008-protect-defaults-') as directory:
        src = Path(directory) / 'defaults.c'
        exe = Path(directory) / ('defaults.exe' if os.name == 'nt' else 'defaults')
        src.write_text(code, encoding='utf-8')
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                       ['-std=c99', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(exe)],
                       check=True)
        result = subprocess.run([str(exe)], check=False).returncode
        return result


if __name__ == '__main__':
    raise SystemExit(main())
