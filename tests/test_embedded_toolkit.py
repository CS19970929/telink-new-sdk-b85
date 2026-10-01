"""Host tool contract tests, with all generated inputs/artifacts outside repo."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

from tools.embedded_toolkit.common import temp_directory
from tools.embedded_toolkit.checks import property_check, regression_scenarios
from tools.embedded_toolkit.reports import write_reports
from tools.embedded_toolkit.scanner import dependency_cycles, functions, scan, write_scan
from tools.embedded_toolkit.simulator import (DEFAULT_INPUT, FakeHardwareBackend, HostSimulator,
                                             load_scenario, sample_command, scenario_program)


class ScenarioValidation(unittest.TestCase):
    def setUp(self):
        self.scenario = {'schema_version': 1, 'name': 'validation', 'steps': [{'at_ms': 0}]}

    def test_unknown_keys_and_types(self):
        for field, value in [('schema_version', True), ('name', 3), ('steps', []), ('typo', 0)]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                scenario_program({**self.scenario, field: value})

    def test_order_grid_bounds_and_duplicate_times(self):
        for steps in ([{'at_ms': 1}], [{'at_ms': -200}], [{'at_ms': 0}, {'at_ms': 0}],
                      [{'at_ms': 200}, {'at_ms': 0}], [{'at_ms': True}], [{'at_ms': 20000200}]):
            with self.subTest(steps=steps), self.assertRaises(ValueError):
                scenario_program({**self.scenario, 'steps': steps})

    def test_sensor_ranges_and_unknown_fields(self):
        for update in ({'cells_mv': []}, {'cells_mv': [61001]}, {'cells_mv': [True]},
                       {'cells_mv': [3300]*33}, {'current_ma': 6553501}, {'battery_temp_decic': []},
                       {'mos_temp_decic': 1051}, {'mos_valid': 1}, {'pack_mv': -1}, {'typo': 1}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                scenario_program({**self.scenario, 'initial': update})

    def test_parameter_width_and_schema(self):
        for params in ({'unknown': [1]*5}, {'cell_ov': [1]*4}, {'cell_ov': [1, 2, 65536, 0, 0]},
                       {'cell_ov': [True]*5}):
            with self.subTest(params=params), self.assertRaises(ValueError):
                scenario_program({**self.scenario, 'parameters': params})

    def test_expected_fields_are_not_silently_ignored(self):
        for expected in ({'charge_on': 2}, {'chg_on': 0}, {'third': -1}, {'first': 4096}):
            with self.subTest(expected=expected), self.assertRaises(ValueError):
                scenario_program({**self.scenario, 'steps': [{'at_ms': 0, 'expect': expected}]})

    def test_units_quantization_and_valid_channels(self):
        state = copy.deepcopy(DEFAULT_INPUT)
        state.update(cells_mv=[3600, 3300], current_ma=-199, mos_temp_decic=-400)
        data = list(map(int, sample_command(state, 0).split()[1:]))
        self.assertEqual(data[:9], [0, 3300, 3600, 690, 0, 1, 650, 650, 0])

    @unittest.skipUnless(importlib.util.find_spec('yaml'), 'PyYAML optional dependency absent')
    def test_yaml_safe_loader(self):
        with temp_directory() as tmp:
            path = Path(tmp) / 'scenario.yaml'
            path.write_text('schema_version: 1\nsteps:\n  - at_ms: 0\n', encoding='utf-8')
            self.assertEqual(len(scenario_program(load_scenario(path))[1]), 1)
            path.write_text('!!python/object/apply:os.system ["echo untrusted"]', encoding='utf-8')
            with self.assertRaises(ValueError):
                load_scenario(path)


class HostBehavior(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.host = HostSimulator().__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.host.__exit__()

    def test_all_boundaries(self):
        for scenario in regression_scenarios():
            with self.subTest(scenario=scenario['name']):
                result = self.host.run(scenario)
                self.assertTrue(result['passed'], result['failures'])

    def test_seeded_properties(self):
        result = property_check(self.host, samples=1000)
        self.assertTrue(result['passed'], result['failures'])

    def test_sample_gap_duplicate_backwards_rejected(self):
        line = sample_command(DEFAULT_INPUT, 0)
        for bad_time in (0, 100, 400, 0xffffffff):
            with self.subTest(time=bad_time):
                result = self.host.execute([line, sample_command(DEFAULT_INPUT, bad_time)])
                self.assertEqual(result[-1]['accepted'], 0)
                self.assertEqual(result[-1]['charge_on'], 0)
                self.assertEqual(result[-1]['discharge_on'], 0)

    def test_backend_failure_report_and_escape(self):
        result = FakeHardwareBackend(self.host).run({'schema_version': 1, 'name': '<script>alert(1)</script>',
                 'steps': [{'at_ms': 0, 'expect': {'charge_on': 1}}]})
        self.assertFalse(result['passed'])
        self.assertFalse(result['hardware_validated'])
        with temp_directory() as tmp:
            write_reports(tmp, [result], self.host.identity, self.host.compiler)
            root = ET.parse(Path(tmp) / 'junit.xml').getroot()
            self.assertEqual(root.attrib['failures'], '1')
            self.assertEqual(len(root.findall('testcase/failure')), 1)
            report = (Path(tmp) / 'report.html').read_text(encoding='utf-8')
            self.assertNotIn('<script>', report)
            self.assertIn('&lt;script&gt;', report)
            self.assertEqual(json.loads((Path(tmp) / 'results.json').read_text(encoding='utf-8'))['failed'], 1)


class RepositoryScanner(unittest.TestCase):
    def test_literals_comments_braces_calls(self):
        source = '/* void fake(void) { } */\nvoid real(void) {\n const char *p="} fake()";\n helper();\n}\n'
        parsed = functions(source)
        self.assertEqual([f['name'] for f in parsed], ['real'])
        self.assertEqual(parsed[0]['calls'], ['helper'])
        self.assertEqual(parsed[0]['line'], 2)

    def test_cycles(self):
        edges = [{'from': a, 'to': b} for a, b in [('a','b'), ('b','a'), ('b','c'), ('c','d')]]
        self.assertEqual(dependency_cycles(edges), [['a', 'b']])
        self.assertEqual(dependency_cycles([]), [])

    def test_membership_ambiguity_globals_encoding_and_outputs(self):
        with temp_directory() as tmp:
            root = Path(tmp)
            for folder in ('src', 'one', 'two'):
                (root / folder).mkdir()
            (root / 'src/a.c').write_text('#include "same.h"\nuint16_t count;\nvoid main_loop(void) {\n count++; delay_ms(1); helper();\n}\n', encoding='utf-8')
            (root / 'one/same.h').write_text('void helper(void) { }\n', encoding='utf-8')
            (root / 'two/same.h').write_bytes('// 温度\nvoid helper(void) { }\n'.encode('gb18030'))
            (root / 'source_order.txt').write_text('src/a.c\nsrc/missing.c\n', encoding='utf-8')
            data = scan(root, root / 'source_order.txt', root)
            self.assertEqual(data['summary']['listed_sources'], 1)
            self.assertEqual(len(data['summary']['missing_sources']), 1)
            self.assertEqual(len(data['unresolved_includes'][0]['candidates']), 2)
            edge = next(e for e in data['call_edges'] if e['call'] == 'helper')
            self.assertFalse(edge['resolved'])
            self.assertEqual(data['global_edges'][0]['global'], 'count')
            self.assertTrue(any(r['kind'] == 'blocking_call' for r in data['risks']))
            write_scan(root / 'report', data)
            for name in ('AI_CONTEXT.md', 'ARCHITECTURE.md', 'MODULE_INDEX.md', 'SYMBOL_INDEX.json',
                         'architecture.json', 'architecture_report.html'):
                self.assertTrue((root / 'report' / name).is_file())

    def test_compile_commands(self):
        with temp_directory() as tmp:
            root = Path(tmp)
            (root / 'a.c').write_text('void a(void) {}\n')
            db = root / 'compile_commands.json'
            db.write_text(json.dumps([{'directory': str(root), 'file': 'a.c', 'command': 'cc -c a.c'}]))
            self.assertEqual(scan(root, compile_commands=db)['summary']['listed_sources'], 1)


if __name__ == '__main__':
    unittest.main()
