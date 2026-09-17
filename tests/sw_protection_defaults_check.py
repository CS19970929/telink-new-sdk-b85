#!/usr/bin/env python3
"""Run the real validator against param.h defaults (no device or Flash writes).

Only SDK includes are removed from param.h. SeriesNum is supplied explicitly;
the D008 no-heater software-temperature default is used. Temporary C/executables
live in the OS temp directory. CC selects the host compiler, as in other tests.
"""
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
    header = re.sub(r'^\s*#include[^\n]*', '',
                    (MOD / 'param.h').read_text(encoding='utf-8'), flags=re.M)
    source = (MOD / 'bms_sw_protection.c').read_text(encoding='utf-8')
    functions = '\n'.join(function(source, signature) for signature in (
        'static uint8_t bms_sw_high_recovery_valid(',
        'static uint8_t bms_sw_low_recovery_valid(',
        'uint8_t bms_sw_protection_validate_params('))
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
    assert(!bms_sw_high_recovery_valid(950, 950));
    assert(!bms_sw_high_recovery_valid(950, 951));
    assert(!bms_sw_low_recovery_valid(400, 400));
    assert(!bms_sw_low_recovery_valid(400, 399));
    puts("PASS alarm boundaries, Third hysteresis/filter, temperature direction and disabled limits");
'''
    # Field names identify the exact existing 0x2100 parameter group. Report all
    # failures rather than just the first short-circuited validator expression.
    groups = [('VcellOvp', 'high'), ('VcellUvp', 'low'),
              ('VbusOvp', 'high'), ('VbusUvp', 'low'),
              ('IchgOcp', 'high'), ('IdsgOcp', 'high'),
              ('TChgOTp', 'high'), ('TchgUTp', 'low'),
              ('TdischgOTp', 'high'), ('TdischgUTp', 'low'),
              ('TmosOTp', 'high'), ('VdeltaOvp', 'high')]
    checks = []
    for name, direction in groups:
        fields = ', '.join('p.u16' + name + suffix for suffix in
                           ('_First', '_Second', '_Third', '_Rcv'))
        order = '>' if direction == 'high' else '<'
        checks.append(f'''if (p.u16{name}_First {order} p.u16{name}_Second || p.u16{name}_Second {order} p.u16{name}_Third) {{
            printf("INVALID ORDER {name}: First=%u Second=%u Third=%u Recover=%u\\n", {fields});
        }}''')
        recovery_fields = f'p.u16{name}_Third, p.u16{name}_Rcv'
        checks.append(f'''if (!bms_sw_{direction}_recovery_valid({recovery_fields})) {{
            printf("INVALID {name}: First=%u Second=%u Third=%u Recover=%u ({direction})\\n", {fields});
        }}''')
    code = ('#include <stdint.h>\n#include <stdio.h>\n#include <assert.h>\ntypedef uint16_t u16;\n'
            f'#define SeriesNum {args.series_num}\n' + header + '\n' + functions +
            '\n' + filters +
            '\nint main(void) {\nstruct PRT_E2ROM_PARAS p = E2P_PROTECT_DEFAULT_PRT;\n' +
            '\n'.join(checks) + '\nint valid = bms_sw_protection_validate_params(&p);\n'
            'printf("Production default validator: %s\\n", valid ? "PASS" : "FAIL");\n' + filter_tests +
            'return valid ? 0 : 1;\n}\n')
    with tempfile.TemporaryDirectory(prefix='d008-protect-defaults-') as directory:
        src = Path(directory) / 'defaults.c'
        exe = Path(directory) / ('defaults.exe' if os.name == 'nt' else 'defaults')
        invalid_defaults = code
        for name, value in (('CUV_1', 3000), ('CUV_2', 3000), ('CUV_3', 3200), ('CUV_recover', 3300)):
            invalid_defaults = re.sub(r'^#define ' + name + r'\s+[^\n]+',
                                      f'#define {name} {value}', invalid_defaults, flags=re.M)
        rejected = subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                                  ['-x', 'c', '-fsyntax-only', '-'], input=invalid_defaults,
                                  text=True, capture_output=True)
        assert rejected.returncode != 0 and 'CUV defaults require' in rejected.stderr
        print('PASS compile-time rejection of 3000/3000/3200/3300 defaults', flush=True)
        src.write_text(code, encoding='utf-8')
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                       ['-std=c99', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(exe)],
                       check=True)
        result = subprocess.run([str(exe)], check=False).returncode
        if args.boot_check:
            # Reuse the RAM Flash fixture, replacing its protection stubs with
            # production defaults/validator. AFE and SOC validators remain mocks;
            # this test isolates software-parameter failure, not those products.
            from d008_storage_host_check import source as store_source
            fixture = (ROOT / 'tests/fixtures/d008_storage/stores.c').read_text()
            fixture = fixture[:fixture.index('static void fresh(void)')]
            fixture = fixture.replace('#define E2P_PROTECT_DEFAULT_PRT {0}', '')
            fixture = fixture.replace('#define PARAM_VER 1u', '')
            fixture = fixture.replace('#define SeriesNum 24u',
                                      f'#define SeriesNum {args.series_num}u')
            fixture = fixture.replace('typedef struct {u16 ParamVer;struct PRT_E2ROM_PARAS protect;} PARAM_T;', '')
            fixture = fixture.replace('static int bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *p){return p!=0;}', functions)
            conf = (MOD / 'conf.h').read_text()
            macros = '\n'.join(re.findall(r'^#define (?:FW_UPGRADE_RESET_\w+|BMS_(?:STATE_SAVE|EVENT_SAVE|STORAGE_RETRY)_INTERVAL_32K)[^\n]*', conf, re.M))
            headers = '\n'.join(store_source(n) for n in (
                'bms_soc_defs.h', 'bms_state_store.h', 'soc_kv_store.h',
                'SocEnhance.h', 'bms_afe_hw_profile.h', 'bms_config_store.h',
                'bms_cold_kv_store.h', 'bms_event_log.h', 'bms_storage_platform.h'))
            units = '\n'.join(store_source(n) for n in (
                'bms_config_store.c', 'bms_state_store.c', 'bms_event_log.c', 'param.c'))
            units = units.replace('struct PRT_E2ROM_PARAS defaults = E2P_PROTECT_DEFAULT_PRT;',
                'struct PRT_E2ROM_PARAS defaults = E2P_PROTECT_DEFAULT_PRT; '
                'if (force_bad_default) { defaults.u16VcellUvp_First=3000; defaults.u16VcellUvp_Second=3000; defaults.u16VcellUvp_Third=3200; defaults.u16VcellUvp_Rcv=3300; }')
            units = 'static int force_bad_default;\n' + units
            fixture = fixture.replace('/* MACROS */', macros).replace('/* TYPES */', header + '\n' + headers).replace('/* PRODUCTION */', units)
            fixture += '''
int main(void) {
    memset(flash, 255, sizeof(flash)); reboot();
    assert(bms_config_store_init());
    assert(bms_config_save_cache(&g_bms_config)); /* Seed an intact journal record. */
    reboot(); errors = 0;
    Param_UpgradeReset_Apply(); LoadParam();
    assert(bms_state_store_init()); assert(bms_event_log_init());
    bms_param_diag_poll(); bms_diag_freeze_boot();
    int valid = bms_sw_protection_validate_params(&g_tParam.protect);
    printf("Production boot: errors=%u Config attempts=%u result=%u first_failure=%u defaults=%u param_result=%u gates=%u\\n",
           (unsigned)errors, bms_diag_cached_word(36), bms_diag_cached_word(38),
           bms_diag_cached_word(37), bms_diag_cached_word(39),
           bms_diag_cached_word(26), bms_diag_cached_word(144));
    assert(bms_diag_cached_word(36) == 1 && bms_diag_cached_word(38) == DIAG_OK);
    assert(bms_diag_cached_word(37) == 0 && bms_diag_cached_word(39) == 0);
    assert(errors == (valid ? 0u : 2u));
    assert(bms_diag_cached_word(144) == (valid ? 3u : 0u));
    if (valid) {
        /* Reproduce the supplied evidence: valid old Flash + rejected new defaults.
         * Do not publish the failed revision or let a later SaveParam clear it. */
        g_bms_config.protect.u16VcellUvp_First=3000;
        g_bms_config.protect.u16VcellUvp_Second=3000;
        g_bms_config.protect.u16VcellUvp_Third=2200;
        g_bms_config.protect.u16VcellUvp_Rcv=3100;
        g_bms_config.control[BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH]=FW_UPGRADE_RESET_PROTECT_EPOCH+1u;
        assert(bms_config_save_cache(&g_bms_config));
        reboot(); force_bad_default=1; errors=0;
        Param_UpgradeReset_Apply(); LoadParam();
        assert(bms_state_store_init()); assert(bms_event_log_init());
        bms_param_diag_poll();
        assert(errors==1 && bms_diag_cached_word(144)==1);
        assert(bms_diag_cached_word(27)==DIAG_UPGRADE_VALIDATION);
        assert(bms_diag_cached_word(28)==DIAG_UPGRADE_BAD_SW);
        assert(bms_diag_cached_word(98)==2200 && bms_diag_cached_word(102)==3200);
        assert(g_bms_config.control[BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH]==FW_UPGRADE_RESET_PROTECT_EPOCH+1u);
        assert(SaveParam() && !bms_protection_params_valid());
        bms_diag_freeze_boot(); bms_diag_upgrade(DIAG_UPGRADE_OK,0);
        assert(bms_diag_cached_word(27)==DIAG_UPGRADE_VALIDATION);
        reboot(); force_bad_default=0;
        Param_UpgradeReset_Apply(); LoadParam(); bms_param_diag_poll();
        assert(bms_diag_cached_word(27)==DIAG_UPGRADE_OK && bms_diag_cached_word(144)==3);
        puts("PASS old valid Flash + invalid CUV defaults: exact gate=1 reproduction, frozen evidence, corrected defaults recover after reboot");
        /* A genuinely invalid Third recovery must still fail closed. */
        g_bms_config.protect.u16TChgOTp_Rcv = g_bms_config.protect.u16TChgOTp_Third;
        assert(bms_config_save_cache(&g_bms_config));
        reboot(); errors = 0;
        Param_UpgradeReset_Apply(); LoadParam(); bms_param_diag_poll();
        assert(errors == 2 && bms_diag_cached_word(144) == 0);
        assert(bms_diag_cached_word(26) == DIAG_INVALID);
        bms_config_store_get_default_protect(&g_tParam.protect);
        assert(SaveParam()); assert(!bms_protection_params_valid());
        reboot(); Param_UpgradeReset_Apply(); LoadParam();
        assert(bms_protection_params_valid());
        puts("PASS invalid Third persisted record: two errors, SaveParam cannot bypass startup gate, reboot recovery");
    }
    return valid ? 0 : 1;
}
'''
            src.write_text(fixture, encoding='utf-8')
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                           ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-Wno-unused-variable', '-I', str(MOD), str(src),
                            str(MOD / 'storage_record.c'), str(MOD / 'bms_diag.c'),
                            '-include', str(MOD / 'bms_diag.h'), '-o', str(exe)], check=True)
            result |= subprocess.run([str(exe)], check=False).returncode
        return result


if __name__ == '__main__':
    raise SystemExit(main())
