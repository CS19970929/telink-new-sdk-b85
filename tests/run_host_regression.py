"""Run all four products against their actual shared sources and selected backend."""
from pathlib import Path
import os
import subprocess
import sys
import json
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PRODUCTS = ("d008", "d011", "d013", "d014")
ALL_PRODUCTS = {
    "modbus_address_host_check.py",
    "bms_diag_contract_check.py", "bms_diag_host_check.py", "soc_contract_check.py",
    "production_policy_check.py", "sw_protection_contract_check.py", "common_feature_policy_contract_check.py",
    "flash_quick_check.py", "afe_hw_access_contract_check.py", "afe_hw_fragment_host_check.py",
    "afe_hw_transaction_host_check.py", "sw_temperature_groups_host_check.py",
}
SH_PRODUCTS = {
    "sh3673510_board_host_check.py", "sh3673510_protection_mode_check.py", "sh3673510_recovery_host_check.py",
    "sh3673510_sample_schedule_host_check.py", "sh3673510_sleep_host_check.py",
    "sh3673510_soc_direction_host_check.py", "sh3673510_temperature_encoding_check.py",
    "sh_event_checkpoint_host_check.py", "sh_storage_platform_host_check.py",
}


def targets(name):
    if name in ALL_PRODUCTS: return PRODUCTS
    if name in SH_PRODUCTS: return PRODUCTS[1:]
    if name == "sh3673510_d011_integration_check.py": return ("d011",)
    if name.startswith(("d008_", "dvc1124_", "app_", "soc_simulator")): return ("d008",)
    return ("d014",)


def main():
    checks = sorted((ROOT / 'tests').glob('*_check.py'))
    if not checks:
        raise RuntimeError('No host checks found')
    commands = [('tooling_unit_tests', p, [sys.executable, '-m', 'unittest', 'tests.test_bms_tools', '-q']) for p in PRODUCTS]
    commands += [(path.stem, p, [sys.executable, str(path)]) for path in checks for p in targets(path.name)]
    output = Path(os.environ.get('BMS_TEST_OUTPUT', str(Path(os.environ.get('LOCALAPPDATA', tempfile.gettempdir())) /
                  'CodexTemp/bms-monorepo-tests' / time.strftime('%Y%m%d-%H%M%S'))))
    if output.resolve().is_relative_to(ROOT): raise ValueError('Test logs must be outside the worktree')
    output.mkdir(parents=True, exist_ok=True)
    failed = []
    results = []
    for name, product, command in commands:
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONUTF8='1', BMS_PRODUCT=product)
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, timeout=180, capture_output=True)
            (output / (name+'-'+product+'.log')).write_bytes(result.stdout+result.stderr)
            code = result.returncode
            if result.returncode:
                failed.append(name+' ('+product+')')
        except subprocess.TimeoutExpired:
            code = 124
            failed.append(name+' ('+product+', timeout)')
        results.append({'check':name,'product':product,'exit':code})
        print(('PASS ' if code==0 else 'FAIL ')+name+' ('+product+')',flush=True)
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf8')
    print('Host regression: %d groups, %d failed; logs: %s' % (len(commands), len(failed), output), flush=True)
    for name in failed:
        print('FAIL ' + name)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
