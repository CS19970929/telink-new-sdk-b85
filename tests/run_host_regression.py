"""Run this product checkout's host checks; never substitute another product's tests."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    checks = sorted((ROOT / 'tests').glob('*_check.py'))
    # Historical D011 board contracts are not valid evidence for other boards.
    product_check = 'd008_framework_contract_check.py'
    if not (ROOT / 'tests' / product_check).is_file():
        raise RuntimeError('Required product contract missing: ' + product_check)
    if not checks:
        raise RuntimeError('No host checks found')
    commands = [('tooling_unit_tests', [sys.executable, '-m', 'unittest', 'tests.test_bms_tools', '-v'])]
    commands += [(path.name, [sys.executable, str(path)]) for path in checks]
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONUTF8='1')
    failed = []
    for name, command in commands:
        print('HOST ' + name, flush=True)
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, timeout=180)
            if result.returncode:
                failed.append(name)
        except subprocess.TimeoutExpired:
            failed.append(name + ' (timeout)')
    print('Host regression: %d groups, %d failed' % (len(commands), len(failed)), flush=True)
    for name in failed:
        print('FAIL ' + name)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
