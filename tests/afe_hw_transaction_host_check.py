"""Exercise production AFE commit/rollback code with bounded fault injection."""
from validation_support import function
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = Sources(ROOT)


def main():
    header = (APP / 'bms_afe_hw_profile.h').read_text(encoding='utf-8')
    header = re.sub(r'^#include[^\n]*', '', header, flags=re.M)
    profile = (APP / 'bms_afe_hw_profile.c').read_text(encoding='utf-8')
    production = profile[profile.index('static u16 s_afe_hw_apply_state'):]
    # Check the actual transport mapping along with the owning transaction.
    modbus = (APP / 'modbus_rtu.c').read_text(encoding='utf-8')
    production += '\n' + function(modbus, 'static u8 afe_hw_profile_write_block(')
    fixture = (ROOT / 'tests/fixtures/afe_hw_transaction.c').read_text(encoding='utf-8')
    fixture = fixture.replace('/* PROFILE_HEADER */', header)
    fixture = fixture.replace('/* PRODUCTION_SOURCE */', production)
    with tempfile.TemporaryDirectory(prefix='afe-hw-transaction-') as directory:
        source = Path(directory) / 'transaction.c'
        executable = Path(directory) / 'transaction.exe'
        source.write_text(fixture, encoding='utf-8')
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c99', '-Wall', '-Wextra', '-Werror', str(source),
            '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == '__main__':
    main()
