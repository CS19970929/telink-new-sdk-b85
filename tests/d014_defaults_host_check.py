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
                            help='Also execute Config/State/Event/bms_parameters_init with RAM Flash')
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

def check_battery_chemistry_defaults():
    """读取实际默认初始化及 AFE builder，覆盖类型与装配组合；不代替完整 TU/实板验证。"""
    import os
    import re
    import shlex
    import subprocess
    from project_paths import host_includes
    from validation_support import ROOT, function, profile_prefix, read, run_c

    print("CHECK battery_chemistry_defaults", flush=True)
    config_h = read('bms/core/bms_config_store.h')
    config_c = read('bms/core/bms_config_store.c')
    # 使用实际字段和默认初始化，不在夹具中另写参数默认表。
    types = ''.join(re.search(r'typedef struct \{[^{}]*\} ' + name + ';', config_h).group(0)
                    for name in ('bms_user_params_t', 'bms_config_system_params_t'))
    defaults = types + '\n' + function(config_c, 'static void bms_config_user_defaults(')
    defaults += '\n' + function(config_c, 'static void bms_config_store_get_default_system(')
    # heater 默认值先由实际功能头提供，再编译其使用者。
    defaults = '#include "bms_features.h"\n' + defaults
    matrix = [('d008', ['-DD008_PRODUCT_PROFILE=3'], 1, 16, 2800),
              ('d008', ['-DD008_PRODUCT_PROFILE=2'], 2, 20, 3000),
              ('d008', ['-DD008_PRODUCT_PROFILE=1'], 1, 24, 2800)]
    matrix += [(product, [] if chemistry == 1 else ['-DBMS_PRODUCT_CHEMISTRY=2'], chemistry, cells, 3000)
               for product, cells in (('d011', 10), ('d013', 10), ('d014', 8))
               for chemistry in (1, 2)]
    for product, flags, chemistry, cells, uvp in matrix:
        nmc = chemistry == 2
        expected = f'''
        enum {{ expected_chemistry={chemistry}, expected_cells={cells},
                expected_cov={4200 if nmc else 3750},
                expected_cov_recover={4100 if nmc else 3500}, expected_cuv={uvp},
                expected_balance={4100 if nmc else 3400},
                expected_pack_ovp1={4200 if nmc else 3500},
                expected_pack_ovp2={4200 if nmc else 3600},
                expected_pack_ovp3={4200 if nmc else 3650},
                expected_pack_uvp3={3000 if nmc else 2900},
                expected_pack_uv_recover={3100 if nmc else 3000} }};
        '''
        code = profile_prefix(product) + '\n' + defaults + expected + r'''
        int main(void)
        {
            bms_protection_params_t sw;
            bms_afe_hw_profile_t hw;
            bms_user_params_t user;
            bms_config_system_params_t system;
            bms_config_store_get_default_protect(&sw);
            bms_afe_hw_profile_build_default(&hw);
            bms_config_user_defaults(&user);
            bms_config_store_get_default_system(&system);
            assert(system.battery_chemistry == expected_chemistry);
            assert(system.soc_profile_id == expected_chemistry);
            assert(system.series_num == expected_cells);
            assert(sw.cell_ovp_first_mv == expected_cov && sw.cell_ovp_second_mv == expected_cov);
            assert(sw.cell_ovp_third_mv == expected_cov && sw.cell_ovp_recover_mv == expected_cov_recover);
            assert(sw.cell_uvp_first_mv == 3000 && sw.cell_uvp_second_mv == 3000);
            assert(sw.cell_uvp_third_mv == expected_cuv && sw.cell_uvp_recover_mv == 3100);
            assert(sw.pack_ovp_first_10mv == expected_pack_ovp1 / 10 * expected_cells);
            assert(sw.pack_ovp_second_10mv == expected_pack_ovp2 / 10 * expected_cells);
            assert(sw.pack_ovp_third_10mv == expected_pack_ovp3 / 10 * expected_cells);
            assert(sw.pack_ovp_recover_10mv == expected_cov_recover / 10 * expected_cells);
            assert(sw.pack_uvp_first_10mv == 300 * expected_cells);
            assert(sw.pack_uvp_second_10mv == 300 * expected_cells);
            assert(sw.pack_uvp_third_10mv == expected_pack_uvp3 / 10 * expected_cells);
            assert(sw.pack_uvp_recover_10mv == expected_pack_uv_recover / 10 * expected_cells);
            assert(hw.cov_mv == expected_cov && hw.cov_recover_mv == expected_cov_recover);
            assert(hw.cuv_mv == expected_cuv && hw.cuv_recover_mv == 3100);
            assert(bms_afe_hw_profile_validate(&hw));
            assert(user.balance_enable == 1 && user.balance_start_mv == expected_balance);
            assert(user.balance_start_delta_mv == 50 && user.balance_stop_delta_mv == 30);
        #if BMS_PRODUCT_ID == 13u
            assert(SH3673510_PRODUCT_BALANCE_SUPPORTED == 0);
        #endif
            puts("battery chemistry defaults: PASS");
            return 0;
        }
        '''
        run_c(code, flags=[*host_includes(ROOT, product), *flags],
              name=f'battery-defaults-{product}-{cells}-{chemistry}')

    # 非法类型、曲线冲突和 D008 装配冲突必须由产品头本身拒绝。
    rejected = [('d014', ['-DBMS_PRODUCT_CHEMISTRY=0'], 'must be LFP'),
                ('d014', ['-DBMS_PRODUCT_CHEMISTRY=3'], 'must be LFP'),
                ('d014', ['-DBMS_PRODUCT_CHEMISTRY=2', '-DBMS_PRODUCT_SOC_PROFILE_ID=1'], 'SOC profile must match'),
                ('d008', ['-DD008_PRODUCT_PROFILE=3', '-DBMS_PRODUCT_CHEMISTRY=2'], 'D008 chemistry must match'),
                ('d008', ['-DD008_PRODUCT_PROFILE=2', '-DBMS_PRODUCT_CHEMISTRY=1'], 'D008 chemistry must match')]
    for product, flags, error in rejected:
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-E', '-x', 'c',
                   *host_includes(ROOT, product), *flags, '-']
        result = subprocess.run(command, input='#include "bms_product.h"\n',
                                capture_output=True, text=True, check=False)
        assert result.returncode != 0 and error in result.stderr, result.stderr


if __name__ == "__main__":
    check_sw_protection_defaults_check()
    check_d014_afe_profile_default_host_check()
    check_battery_chemistry_defaults()
