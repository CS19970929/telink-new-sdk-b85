"""源码窗口保存边界、共享参数回滚及真实目录输入；不连接设备。"""
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'bms_tools'))
import configure_parameters as editor
from tests.test_bms_tools import copy_configuration


class EditorTests(unittest.TestCase):
    def test_ratio_ternary_and_source_preset_compile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            copy_configuration(root)
            with mock.patch.object(editor, 'ROOT', root):
                config = editor.Configuration('d014', 'lfp')
                row = next(r for r in config.rows if r['name'] == 'BMS_SW_CELL_OVP_FIRST_MV_FOR')
                row['edit'] = str(row['value'] + 1)
                config.presets[0x46] = (1, 1)
                changes = config.replacements(2000, 3, False)
                shared = root / editor.build_options.SHARED_PARAMETERS
                before = editor.Configuration('d014', 'nmc')
                config.save(changes)
                after = editor.Configuration('d014', 'nmc')
                self.assertEqual([(r['name'], r['value']) for r in before.rows],
                                 [(r['name'], r['value']) for r in after.rows])
                self.assertIn(str(row['value'] + 1) + 'u', shared.read_text(encoding='utf8'))
                self.assertEqual((after.element, after.parallel), (2000, 3))
                source = '#include "afe_register_config.h"\n#define SET(r,m,v) {r,m,v},\n'
                source += 'static const unsigned a[][3]={ BMS_AFE_REGISTER_PRESETS(SET) };\n'
                source += 'typedef char count_matches[sizeof(a)/sizeof(a[0])==BMS_AFE_REGISTER_PRESET_COUNT?1:-1];\n'
                result = subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c99', '-fsyntax-only',
                    '-I', str(config.folder), '-x', 'c', '-'], input=source, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_external_edit_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            copy_configuration(root)
            with mock.patch.object(editor, 'ROOT', root):
                config = editor.Configuration('d014', 'lfp')
                changes = config.replacements(2000, 3, True)
                path = config.folder / 'afe_register_config.h'
                external = path.read_bytes() + b'\n/* external edit */\n'
                path.write_bytes(external)
                with self.assertRaisesRegex(ValueError, '重新载入'):
                    config.save(changes)
                self.assertEqual(path.read_bytes(), external)

    def test_shared_save_failure_rolls_back_policy_and_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            copy_configuration(root)
            editor.build_options.save_source(root, 'd011', {'update_groups': ['factory_reset']})
            before = {p: p.read_bytes() for p in (root/'bms/products').rglob('*.h')}
            with mock.patch.object(editor, 'ROOT', root):
                config = editor.Configuration('d014', 'lfp')
                next(r for r in config.rows if r['name'] == 'BMS_SW_CELL_OVP_FIRST_MV_FOR')['edit'] = '3561'
                save = editor.build_options.save_source
                def fail_second(root, product, changes):
                    if product == 'd013': raise IOError('injected second product failure')
                    return save(root, product, changes)
                with mock.patch.object(editor.build_options, 'save_source', side_effect=fail_second):
                    with self.assertRaisesRegex(IOError, 'injected'):
                        config.save(config.replacements(2000, 3, False))
                self.assertEqual({p: p.read_bytes() for p in before}, before)

    def test_catalog_matches_original_reference_indexes(self):
        from afe_register_catalog import catalog
        self.assertEqual(catalog(), editor.CATALOG)
        self.assertEqual([len(f['registers']) for f in editor.CATALOG['families']], [145, 90, 90])
        self.assertTrue(next(r for r in editor.CATALOG['families'][1]['registers'] if r['address'] == 0x90)['readable'])
        for family in editor.CATALOG['families']:
            for register in family['registers']:
                self.assertEqual(register['source_mask'] & ~register['write_mask'], 0)
                if register['read_clear']: self.assertEqual(register['write_mask'], 0)


if __name__ == '__main__':
    unittest.main()
