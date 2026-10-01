"""Provenance and safe artifact locations shared by the offline tools."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=30)
    return result.stdout.strip() if result.returncode == 0 else None


def provenance(root, paths=()):
    return {'commit': git(root, 'rev-parse', 'HEAD'),
            'branch': git(root, 'branch', '--show-current'),
            'dirty': bool(git(root, 'status', '--porcelain')),
            'source_sha256': {str(p.relative_to(root)).replace('\\', '/'):
                              hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}


def temp_directory():
    base = Path(os.environ.get('LOCALAPPDATA', tempfile.gettempdir())) / 'CodexTemp' / 'embedded-toolkit'
    base.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(prefix='host-', dir=base)


def default_output(name):
    return Path.home() / 'Documents' / 'CodexOutputs' / 'embedded-toolkit' / name


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
