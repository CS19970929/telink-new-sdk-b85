"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sh3673510_recovery_host_check():
    print("CHECK sh3673510_recovery_host_check", flush=True)
    """Execute production SH recovery/FET functions with deterministic fault injection.

    No register behavior is emulated: the fixture supplies sampled flags and command
    outcomes. This checks MCU policy, not SPI timing or physical FET conduction.
    """
    from validation_support import function as extract_function
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import os
    import re
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    source = (APP / 'sh3673510_bms.c').read_text(encoding='utf-8')


    def function(name):
        match = re.search(r'(?m)^(?:static )?(?:void|uint8_t|uint16_t) ' + name + r'\(', source)
        assert match, name
        return extract_function(source, match.group(0))

    # 用首个函数的代码签名界定状态区，不依赖可翻译的说明文字。
    state = source[source.index('#define SH3510_SAMPLE_MS'):source.index('static void restart_sampling(void)')]
    functions = '\n'.join(function(name) for name in (
        'restart_sampling', 'filter_samples', 'note_comm_error', 'charge_blocked', 'discharge_blocked',
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
                            *host_includes(ROOT), str(c), '-o', str(exe)], check=True)
            if subprocess.run([str(exe)], check=False).returncode:
                failed_modes.append(hw)
    if failed_modes:
        raise AssertionError('production recovery failures in HW modes: ' + str(failed_modes))
    print('SH3673510 production recovery/FET fault injection HW=0/1: PASS')

def check_sh_recovery_evidence_host_check(defines=None):
    print("CHECK sh_recovery_evidence_host_check", flush=True)
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


    if defines is not None:
        run(defines)
    else:
        run()
        run(('-DGUARD_TEST=1',))
        run(('-DMODE_TEST=1',))

if __name__ == "__main__":
    check_sh3673510_recovery_host_check()
    check_sh_recovery_evidence_host_check()
