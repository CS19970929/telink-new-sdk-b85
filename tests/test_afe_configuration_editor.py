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
    @staticmethod
    def writable_sh_fixture(config):
        """测试保存流程使用显式合法候选，不依赖产品保留的历史默认。"""
        if config.product == 'd008':
            return
        from fractions import Fraction
        from afe_source_fields import ceil_fraction
        shunt = Fraction(config.element, config.parallel)
        values = dict(OCD1_A10=ceil_fraction(50000 / shunt), OCD1_DELAY_MS=140,
                      OCD2_A10=ceil_fraction(100000 / shunt), OCD2_DELAY_MS=25,
                      OCC1_A10=ceil_fraction(13750 / shunt), OCC1_DELAY_MS=140,
                      OCD_RECOVER_A10=0, OCC_RECOVER_A10=0,
                      CHG_UT_X10=200, CHG_UT_RECOVER_X10=300)
        base = ceil_fraction(Fraction(values['OCD2_A10']) * shunt / 100000) * 100000 / shunt
        values['SC_A10'] = ceil_fraction(base * 2)
        for row in config.rows:
            name = row['name'].removeprefix('SH3673510_HW_DEFAULT_')
            if name in values:
                row['edit'] = str(values[name])

    def test_display_units_preserve_source_integers(self):
        cases = (
            ('BMS_SW_CHARGE_OCP_FIRST_A10', 125, '12.5', 'A'),
            ('BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT', 200, '0.2', 'A'),
            ('BMS_SW_CHARGE_UTP_THIRD_X10', 350, '-5', '℃'),
            ('BMS_HEATER_START_TEMP_X10', 400, '0', '℃'),
            ('BMS_SLEEP_LOW_SECONDS', 3600, '60', '分钟'),
            ('BMS_SLEEP_LOW_SECONDS', 1, '1/60', '分钟'),
            ('BMS_SW_CELL_OVP_FILTER_10MS', 100, '1000', 'ms'),
            ('SH3673510_HW_DEFAULT_COV_MV_FOR', 3650, '3650', 'mV'),
        )
        for name, raw, display, unit in cases:
            with self.subTest(name=name, raw=raw):
                self.assertEqual(editor.unit(name), unit)
                self.assertEqual(editor.display_value(name, raw), display)
                self.assertEqual(editor.source_value(name, display), str(raw))
        for name, value in (('D008_AFE_OCD1_A10', '1.23'),
                            ('BMS_HEATER_START_TEMP_X10', '-40.1'),
                            ('BMS_SLEEP_LOW_SECONDS', '0.001')):
            with self.assertRaises(ValueError): editor.source_value(name, value)

    def test_saved_changes_include_their_ota_groups(self):
        for product in editor.build_options.PRODUCTS:
            with self.subTest(product=product), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                copy_configuration(root)
                editor.build_options.save_source(root, product, {'update_groups': ['business']})
                with mock.patch.object(editor, 'ROOT', root):
                    config = editor.Configuration(product, 'lfp')
                    prefix = 'D008_SW_' if product == 'd008' else 'BMS_SW_'
                    next(row for row in config.rows if row['name'].startswith(prefix))['edit'] = '3541'
                    config.save(config.replacements(config.element, config.parallel, config.debug))
                    self.assertIn('sw', editor.build_options.verify_source(root, product)['update_groups'])
                    config = editor.Configuration(product, 'lfp')
                    self.writable_sh_fixture(config)
                    hardware = next(row for row in config.rows if '_COV_MV' in row['name'] and
                                    row['name'].startswith(('D008_AFE_', 'SH3673510_HW_DEFAULT_')))
                    hardware['edit'] = str(hardware['value'] + 1)
                    edits = config.replacements(config.element, config.parallel, config.debug)
                    self.assertIn('afe', config.update_groups_for_changes(edits))
                    config.save(edits)
                    self.assertIn('afe', editor.build_options.verify_source(root, product)['update_groups'])
                    config = editor.Configuration(product, 'lfp')
                    config.settings['chemistry'] = 'nmc' if config.settings['chemistry']=='lfp' else 'lfp'
                    config.save(config.replacements(config.element, config.parallel, config.debug))
                    self.assertEqual(editor.build_options.verify_source(root, product)['update_groups'],
                                     ['sw','afe','business','soc'])
                    editor.build_options.save_source(root, product, {'update_groups':['factory_reset']})
                    config = editor.Configuration(product, 'lfp')
                    config.settings['capacity_0p1ah'] += 1
                    config.save(config.replacements(config.element, config.parallel, config.debug))
                    self.assertEqual(editor.build_options.verify_source(root, product)['update_groups'], ['factory_reset'])

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
                product_parameters = config.folder / 'd014_protection_config.h'
                before = editor.Configuration('d014', 'nmc')
                config.save(changes)
                after = editor.Configuration('d014', 'nmc')
                self.assertEqual([(r['name'], r['value']) for r in before.rows],
                                 [(r['name'], r['value']) for r in after.rows])
                self.assertIn(str(row['value'] + 1) + 'u', product_parameters.read_text(encoding='utf8'))
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

    def test_selected_product_save_failure_rolls_back_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            copy_configuration(root)
            before = {p: p.read_bytes() for p in (root/'bms/products').rglob('*.h')}
            with mock.patch.object(editor, 'ROOT', root):
                config = editor.Configuration('d014', 'lfp')
                next(r for r in config.rows if r['name'] == 'BMS_SW_CELL_OVP_FIRST_MV_FOR')['edit'] = '3561'
                with mock.patch.object(editor.build_options, 'save_source', side_effect=IOError('injected product failure')):
                    with self.assertRaisesRegex(IOError, 'injected'):
                        config.save(config.replacements(2000, 3, False))
                self.assertEqual({p: p.read_bytes() for p in before}, before)

    def test_four_products_edit_independently_and_preserve_both_chemistries(self):
        for product in editor.build_options.PRODUCTS:
            with self.subTest(product=product),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);copy_configuration(root)
                before={p:p.read_bytes() for p in (root/'bms/products').rglob('*.h')}
                with mock.patch.object(editor,'ROOT',root):
                    config=editor.Configuration(product,'lfp')
                    name='D008_SW_CELL_OVP_FIRST_MV_FOR' if product=='d008' else 'BMS_SW_CELL_OVP_FIRST_MV_FOR'
                    row=next(r for r in config.rows if r['name']==name);row['edit']='3541'
                    config.change_chemistry('nmc');row['edit']='4141'
                    config.save(config.replacements(config.element,config.parallel,config.debug))
                    for chemistry,value in [('lfp',3541),('nmc',4141)]:
                        actual=editor.Configuration(product,chemistry)
                        self.assertEqual(next(r for r in actual.rows if r['name']==name)['value'],value)
                for path,raw in before.items():
                    if f'/{product}/' not in path.as_posix():self.assertEqual(path.read_bytes(),raw,str(path))

    def test_customer_profile_round_trip_and_cross_product_rejection(self):
        import copy
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);copy_configuration(root)
            with mock.patch.object(editor,'ROOT',root):
                config=editor.Configuration('d014','lfp')
                self.writable_sh_fixture(config)
                next(r for r in config.rows if r['name']=='BMS_SW_CELL_OVP_FIRST_MV_FOR')['edit']='3561'
                config.presets[0x46]=(1,1)
                profile=config.profile(2000,3,False,'客户 A','v2')
                self.assertEqual(profile['parameters']['BMS_SW_CELL_OVP_FIRST_MV_FOR']['lfp'],3561)
                self.assertNotIn('update_groups',profile['assembly'])
                before={p:p.read_bytes() for p in (root/'bms/products').rglob('*.h')}
                other=editor.Configuration('d014','nmc');other.load_profile(profile)
                self.assertEqual(before,{p:p.read_bytes() for p in before})
                self.assertEqual(other.profile(2000,3,False,'客户 A','v2')['parameters'],profile['parameters'])
                other.save(other.replacements(other.element,other.parallel,other.debug))
                self.assertEqual(editor.build_options.source_customer_profile(root,'d014'),{'name':'客户 A','revision':'v2'})
                other=editor.Configuration('d011','lfp')
                with self.assertRaisesRegex(ValueError,'产品'):other.load_profile(profile)
                for mutation in ('unknown_parameter','bad_preset','wrong_catalog','missing_chemistry','invalid_temperature'):
                    broken=copy.deepcopy(profile)
                    if mutation=='unknown_parameter':broken['parameters']['BMS_PRODUCT_DEFAULT_SERIAL']=1
                    elif mutation=='bad_preset':broken['presets']=[{'address':0x41,'mask':3,'value':3}]
                    elif mutation=='wrong_catalog':broken['catalog_id']='00000000'
                    elif mutation=='missing_chemistry':del broken['parameters']['BMS_SW_CELL_OVP_FIRST_MV_FOR']['nmc']
                    else:broken['parameters']['SH3673510_HW_DEFAULT_CHG_UT_X10']=150
                    candidate=editor.Configuration('d014','lfp')
                    previous=[r['edit'] for r in candidate.rows]
                    with self.assertRaises(ValueError):candidate.load_profile(broken)
                    self.assertEqual([r['edit'] for r in candidate.rows],previous)

    def test_d014_complete_write_rejects_unrelated_invalid_temperature(self):
        config = editor.Configuration('d014', 'lfp')
        self.writable_sh_fixture(config)
        rows = {r['name']: r for r in config.rows}
        rows['SH3673510_HW_DEFAULT_CHG_UT_X10']['edit'] = '150'
        rows['SH3673510_HW_DEFAULT_OCC1_DELAY_MS']['edit'] = '100'
        rows['SH3673510_HW_DEFAULT_CUV_MV_FOR']['edit'] = '2500'
        before = {p: p.read_bytes() for p in config.paths}
        with self.assertRaises(ValueError) as result:
            config.replacements(2000, 3, False)
        self.assertIn('CHG_UT_X10=150', str(result.exception))
        self.assertIn('OCC1_DELAY_MS=100', str(result.exception))
        self.assertEqual({p: p.read_bytes() for p in config.paths}, before)
        rows['SH3673510_HW_DEFAULT_CHG_UT_X10']['edit'] = '200'
        rows['SH3673510_HW_DEFAULT_OCC1_DELAY_MS']['edit'] = '140'
        self.assertTrue(config.replacements(2000, 3, False))
        rows['SH3673510_HW_DEFAULT_CUV_MV_FOR']['edits']['nmc'] = 3501
        with self.assertRaisesRegex(ValueError, 'NMC / CUV_MV'):
            config.replacements(2000, 3, False)

    def test_sh_write_scalar_boundaries_and_current_dependencies(self):
        from afe_source_fields import SourceProtectionFields
        config = editor.Configuration('d014', 'lfp')
        self.writable_sh_fixture(config)
        codec = SourceProtectionFields(config)
        # 消除边界测试的回差干扰，恢复字段仍通过真实范围检查。
        for name, value in dict(COV_RECOVER_MV_FOR=0, CUV_RECOVER_MV_FOR=65535,
                CHG_OT_RECOVER_X10=0, DSG_OT_RECOVER_X10=0,
                CHG_UT_RECOVER_X10=1450, DSG_UT_RECOVER_X10=1450).items():
            row = next(r for r in config.rows if r['name']=='SH3673510_HW_DEFAULT_'+name)
            if row['ternary']:
                row['edits'] = dict(lfp=value, nmc=value)
            row['edit'] = str(value)
        limits = [('COV_MV',3000,4500),('CUV_MV',1000,3500),('COV_DELAY_MS',140,10010),
                  ('CUV_DELAY_MS',490,10010),('OCD1_DELAY_MS',140,10010),('OCD2_DELAY_MS',25,400),
                  ('OCC1_DELAY_MS',140,10010),('SC_DELAY_US',0,576),('CHG_OT_X10',800,1100),
                  ('CHG_UT_X10',200,500),('DSG_OT_X10',850,1200),('DSG_UT_X10',0,500)]
        for name, minimum, maximum in limits:
            row = codec.row(name)
            original = row['edit'], dict(row['edits'])
            for value in (minimum-1, minimum, maximum, maximum+1):
                with self.subTest(name=name, value=value):
                    row['edit'] = str(value)
                    if row['ternary']:
                        row['edits']['lfp'] = value
                    self.assertEqual(not codec.sh_write_validation_errors('lfp'), minimum <= value <= maximum)
            row['edit'], row['edits'] = original
        codec.set('OCD2_A10', 450)
        codec.set('SC_A10', 600)
        self.assertTrue(any('SC_A10' in e for e in codec.sh_write_validation_errors('lfp')))
        codec.set('SC_A10', 900)
        self.assertFalse(codec.sh_write_validation_errors('lfp'))
        codec.set('ENABLE_OCD1', 0)
        self.assertTrue(any('共用' in e for e in codec.sh_write_validation_errors('lfp')))

    def test_sh_charge_temperature_register_field_uses_own_range(self):
        from afe_source_fields import SourceProtectionFields
        config=editor.Configuration('d014','lfp')
        codec=SourceProtectionFields(config)
        # OTC允许40..70℃；以前错误地沿用OTD的45..80℃搜索范围。
        code=codec.temp_code(800, False)
        codec.edit(0x51,255,0,code)
        self.assertTrue(800 <= codec.value('CHG_OT_X10') <= 1100)
        self.assertEqual(codec.registers()[0x51], code)

    def test_register_field_bits_update_physical_owner(self):
        from afe_source_fields import SourceProtectionFields
        for product in ('d008','d014'):
            config=editor.Configuration(product,'lfp');codec=SourceProtectionFields(config)
            cases=((0x5B,255,0,20),(0x63,255,0,42),(0x71,15,0,10)) if product=='d008' else ((0x4D,15,0,0),(0x4F,15,0,11),(0x49,112,4,3))
            for address,mask,shift,value in cases:
                codec.edit(address,mask,shift,value)
                self.assertEqual((codec.registers()[address]&mask)>>shift,value)
            self.assertFalse(config.presets)  # 保护位只修改参数，没有第二份 raw 覆盖。

    def test_catalog_matches_original_reference_indexes(self):
        from afe_register_catalog import catalog
        self.assertEqual(catalog(), editor.CATALOG)
        self.assertEqual([len(f['registers']) for f in editor.CATALOG['families']], [145, 90, 90])
        self.assertTrue(next(r for r in editor.CATALOG['families'][1]['registers'] if r['address'] == 0x90)['readable'])
        self.assertTrue(next(r for r in editor.CATALOG['families'][1]['registers'] if r['address'] == 0x5A)['read_clear'])
        dvc={r['address']:r for r in editor.CATALOG['families'][0]['registers']}
        self.assertEqual(dvc[0x70]['fields'][0]['logical_width'],12)
        self.assertIn('reg_0x71',dvc[0x70]['fields'][0]['combine_code'])
        self.assertEqual(dvc[0x6D]['write_mask'] & 4,0)
        self.assertEqual(dvc[0x77]['write_mask'] & 0x20,0)
        self.assertTrue(all(dvc[a]['source_mask']==0 for a in (0x6A,0x6B,0x6C,0x6E,0x77)))
        config=editor.Configuration('d008','lfp');config.presets[0x52]=(31,31)
        with self.assertRaisesRegex(ValueError,'N/A'):config.replacements(config.element,config.parallel,False)
        for family in editor.CATALOG['families']:
            for register in family['registers']:
                self.assertEqual(register['source_mask'] & ~register['write_mask'], 0)
                if register['read_clear']: self.assertEqual(register['write_mask'], 0)


if __name__ == '__main__':
    unittest.main()
