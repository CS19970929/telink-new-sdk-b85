"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sw_protection_defaults_check():
    print("CHECK sw_protection_defaults_check", flush=True)
    #!/usr/bin/env python3
    """Execute common First/Second, Third and directional temperature filter boundaries."""
    import argparse
    import os
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    from validation_support import function
    import re
    import shlex
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    MOD = Sources(ROOT)


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
        assert main() in (None, 0)

def check_d014_afe_profile_default_host_check():
    print("CHECK d014_afe_profile_default_host_check", flush=True)
    #!/usr/bin/env python3
    """Execute D014's actual product defaults and validator; no copied board macros."""
    from project_paths import host_includes
    from validation_support import ROOT, profile_prefix, run_c

    code = profile_prefix('d014') + """
    int main(void)
    {
        bms_afe_hw_profile_t p;
        bms_afe_hw_profile_build_default(&p);
        if (p.ocd1_a10 != SH3673510_HW_DEFAULT_OCD1_A10 ||
            p.ocd_recover_a10 != SH3673510_HW_DEFAULT_OCD_RECOVER_A10 ||
            p.occ1_a10 != SH3673510_HW_DEFAULT_OCC1_A10 ||
            p.occ_recover_a10 != SH3673510_HW_DEFAULT_OCC_RECOVER_A10 ||
            !bms_afe_hw_profile_validate(&p)) return 1;
        p.ocd_recover_a10 = sh3673510_quantize_current_a10(p.ocd1_a10, SH3673510_BOARD_SHUNT_UOHM, 5000u, 15u, 0);
        if (bms_afe_hw_profile_validate(&p)) return 2;
        p.ocd_recover_a10 = SH3673510_HW_DEFAULT_OCD_RECOVER_A10;
        p.occ_recover_a10 = sh3673510_quantize_current_a10(p.occ1_a10, SH3673510_BOARD_SHUNT_UOHM, 1375u, 31u, 0);
        if (bms_afe_hw_profile_validate(&p)) return 3;
        puts("D014 independent AFE defaults and effective recovery boundaries: PASS");
        return 0;
    }
    """
    run_c(code, flags=host_includes(ROOT, 'd014'), name='d014-afe-defaults')

if __name__ == "__main__":
    check_sw_protection_defaults_check()
    check_d014_afe_profile_default_host_check()
