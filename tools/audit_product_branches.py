"""Read-only audit of committed product contracts; does not checkout or fetch.

Run from any worktree. Common source parity is a regression gate, not proof of
equivalent hardware or behavior. Product/AFE-specific implementations are never
required to match. Uncommitted source is deliberately outside this report.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
APP = 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/'
DEFAULT_REFS = {p: f'refactor/{p.lower()}-common-bms-features'
                for p in ('D008', 'D011', 'D013', 'D014')}
SH = ('D011', 'D013', 'D014')
SHARED_FILES = ('SocEnhance.c', 'SocEnhance.h', 'storage_record.c', 'bms_state_store.c',
                'bms_sw_protection.c', 'bms_sw_protection.h',
                'bms_stack_monitor.c', 'bms_stack_monitor.h')
SH_FILES = ('bms_features.c', 'bms_afe_guard.c', 'bms_event_log.c',
            'bms_storage_platform_telink.c', 'sh3673510_ntc.c', 'sh3673510_ntc.h')
TOOL_FUNCTIONS = ('_git_provenance', '_map_symbol_value', '_listing_abs_symbol_value', 'cmd_map', '_capture_compile_inputs', '_read_compile_inputs', '_write_compile_inputs', '_mark_build_complete')


def git(*args):
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.decode('utf-8', errors='replace').strip())
    return result.stdout.decode('utf-8').replace('\r\n', '\n')


def digest(source):
    return hashlib.sha256(source.encode('utf-8')).hexdigest()


def audit(refs, worktrees=None):
    worktrees = worktrees or {}
    products, failures = {}, []
    for product, ref in refs.items():
        commit = git('rev-parse', '--verify', ref + '^{commit}').strip()
        read = (lambda path: (worktrees[product] / path).read_text(encoding='utf-8')) if product in worktrees else (lambda path: git('show', commit + ':' + path))
        order = read('bms_tools/source_order.txt').splitlines()
        files = SHARED_FILES + (SH_FILES if product in SH else ())
        app = read(APP + 'app.c')
        hashes = {}
        for file in files:
            if file.endswith('.c') and 'vendor/ble_sample/' + file not in order:
                failures.append(f'{product}: {file} absent from fixed source order')
            hashes[file] = digest(read(APP + file))
        scheduler = all(token in app for token in (
            'bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup)',
            'bls_pm_setAppWakeupLowPower(', '    app_sample_task();'))
        if product in SH and not scheduler:
            failures.append(f'{product}: missing PM-aware sample scheduler integration')
        tool_tree = ast.parse(read('bms_tools/bms.py'))
        provenance = next((n for n in tool_tree.body if isinstance(n, ast.FunctionDef)
                           and n.name == '_git_provenance'), None)
        if provenance is None:
            raise RuntimeError(f'{product}: missing Git provenance implementation')
        tool_hashes = {name: digest(ast.dump(next(n for n in tool_tree.body if isinstance(n, ast.FunctionDef) and n.name == name), include_attributes=False)) for name in TOOL_FUNCTIONS}
        policy = {n.targets[0].id: ast.literal_eval(n.value) for n in tool_tree.body if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name) and n.targets[0].id in ('STARTUP_SRAM_END','MAIN_STACK_RESERVE_BYTES')}
        if policy != {'STARTUP_SRAM_END': 0x848000, 'MAIN_STACK_RESERVE_BYTES': 3072}:
            failures.append(f'{product}: invalid SRAM/stack budget policy')
        linker = read(APP.split('vendor/')[0] + 'project/tlsr_tc32/B85/boot.link')
        if '__SRAM_SIZE - 3072' not in linker or '__SRAM_SIZE == 0x848000' not in linker:
            failures.append(f'{product}: linker lacks startup/stack hard gate')
        platform_core = read(APP+'bms_storage_platform_telink.c').split('uint32_t bms_diag_tick(void)')[0].rstrip()
        products[product] = {
            'ref': ref, 'commit': commit,
            'workingTree': str(worktrees[product]) if product in worktrees else None,
            'resourceToolAstSha256': tool_hashes,
            'storagePlatformCoreSha256': digest(platform_core),
            'sourceOrderSha256': digest('\n'.join(order)),
            'hashesLfNormalized': hashes,
            'pmSampleScheduler': scheduler if product in SH else None,
            'developmentSocInputRecorder': 'BMS_SOC_RECORD_ENABLE' in read(APP + 'SocEnhance.h'),
            'gitProvenanceAstSha256': digest(ast.dump(provenance, include_attributes=False)),
        }
    for file, members in [(f, tuple(refs)) for f in SHARED_FILES] + [(f, SH) for f in SH_FILES]:
        if len({products[p]['hashesLfNormalized'][file] for p in members}) != 1:
            failures.append(f'Unexpected common source drift: {file} ({", ".join(members)})')
    for name in TOOL_FUNCTIONS:
        if len({p['resourceToolAstSha256'][name] for p in products.values()}) != 1:
            failures.append('Unexpected tooling drift: ' + name)
    if len({p['storagePlatformCoreSha256'] for p in products.values()}) != 1:
        failures.append('Unexpected shared Flash transaction platform drift')
    if len({p['gitProvenanceAstSha256'] for p in products.values()}) != 1:
        failures.append('Unexpected Git provenance implementation drift')
    groups = {}
    for product in refs:
        key = products[product]['hashesLfNormalized']['bms_sw_protection.c']
        groups.setdefault(key, []).append(product)
    return {
        'schema': 'bms-product-contract-audit/v1',
        'scope': 'explicit worktrees override refs; no hardware evidence; no automatic synchronization',
        'products': products,
        'observedSoftwareProtectionGroups': list(groups.values()),
        'unexpectedDrift': failures,
        'ok': not failures,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', action='append', default=[], metavar='PRODUCT=REF',
                        help='override a product ref (branch, tag, or commit); repeatable')
    parser.add_argument('--worktree', action='append', default=[], metavar='PRODUCT=PATH',
                        help='read candidate source in explicit worktree, without claiming it is committed')
    parser.add_argument('--check', action='store_true', help='exit 1 on unexpected contract drift')
    args = parser.parse_args()
    refs = dict(DEFAULT_REFS)
    for value in args.ref:
        product, separator, ref = value.partition('=')
        if not separator or product not in refs or not ref or ref.startswith('-'):
            parser.error('--ref must be D008/D011/D013/D014=REF')
        refs[product] = ref
    worktrees = {}
    for value in args.worktree:
        product, separator, path = value.partition('=')
        if not separator or product not in refs or not Path(path).is_dir():
            parser.error('--worktree must be PRODUCT=existing-directory')
        worktrees[product] = Path(path).resolve()
    try:
        result = audit(refs, worktrees)
    except (RuntimeError, UnicodeError) as ex:
        print(json.dumps({'schema': 'bms-product-contract-audit/v1', 'ok': False,
                          'error': str(ex)}, ensure_ascii=False, indent=2))
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if args.check and not result['ok'] else 0


if __name__ == '__main__':
    sys.exit(main())
