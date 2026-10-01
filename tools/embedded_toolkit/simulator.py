"""Scenario runner and fake HIL using the actual production protection module."""
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess

from .common import APP, ROOT, provenance, temp_directory
from .compiler import compile_c

PROTECTIONS = ('cell_ov', 'cell_uv', 'pack_ov', 'pack_uv', 'charge_oc', 'discharge_oc',
               'charge_ot', 'charge_ut', 'discharge_ot', 'discharge_ut', 'mos_ot', 'cell_delta')
DEFAULT_INPUT = {'cells_mv': [3300] * 8, 'current_ma': 0,
                 'battery_temp_decic': [250, 250], 'mos_temp_decic': 250,
                 'battery_valid': True, 'mos_valid': True, 'communication_ok': True,
                 'charger_present': False, 'load_present': False,
                 'charge_request': True, 'discharge_request': True, 'output_enabled': True,
                 'afe_charge_block': False, 'afe_discharge_block': False,
                 'short_active': False, 'physical_release': False}
BOOL_FIELDS = {k for k, v in DEFAULT_INPUT.items() if isinstance(v, bool)}
EXPECT_FIELDS = {'first', 'second', 'third', 'charge_on', 'discharge_on',
                 'temp_break', 'short_latched', 'accepted'}


def integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{name}: expected integer {low}..{high}, got {value!r}')
    return value


def strict_keys(value, allowed, name):
    if not isinstance(value, dict) or set(value) - set(allowed):
        raise ValueError(f'{name}: unknown fields or not an object')


def load_scenario(path):
    path = Path(path)
    text = path.read_text(encoding='utf-8-sig')
    if path.suffix.lower() in ('.yaml', '.yml'):
        try:
            import yaml
        except ImportError as exc:
            raise ValueError('YAML needs PyYAML; JSON works without dependencies') from exc
        try:
            return yaml.safe_load(text)
        except yaml.YAMLError as exc:
            raise ValueError('invalid YAML scenario: ' + str(exc)) from exc
    return json.loads(text)


def parameter_commands(parameters):
    strict_keys(parameters, PROTECTIONS, 'parameters')
    commands = []
    for name, values in parameters.items():
        if not isinstance(values, list) or len(values) != 5:
            raise ValueError(f'{name}: [First, Second, Third, Recover, Filter_10ms] required')
        checked = [integer(v, 0, 65535, name) for v in values]
        commands.append('P ' + str(PROTECTIONS.index(name)) + ' ' + ' '.join(map(str, checked)))
    return commands


def sample_command(state, time_ms):
    strict_keys(state, DEFAULT_INPUT.keys() | {'pack_mv'}, 'input')
    cells = state['cells_mv']
    temps = state['battery_temp_decic']
    if not isinstance(cells, list) or not 1 <= len(cells) <= 32:
        raise ValueError('cells_mv: 1..32 valid cells required; 61001 slots must be omitted')
    cells = [integer(v, 0, 6000, 'cell_mv') for v in cells]
    if not isinstance(temps, list) or not 1 <= len(temps) <= 8:
        raise ValueError('battery_temp_decic: 1..8 temperatures required')
    temps = [integer(v, -400, 1050, 'battery_temp_decic') + 400 for v in temps]
    mos = integer(state['mos_temp_decic'], -400, 1050, 'mos_temp_decic') + 400
    current = integer(state['current_ma'], -6553500, 6553500, 'current_ma')
    pack = integer(state.get('pack_mv', sum(cells)), 0, 655350, 'pack_mv')
    for field in BOOL_FIELDS:
        if type(state[field]) is not bool:
            raise ValueError(field + ': boolean required')
    # Explicit quantization to existing firmware units, no rounding toward trip.
    values = [time_ms & 0xffffffff, min(cells), max(cells), pack // 10,
              max(current, 0) // 100, max(-current, 0) // 100,
              min(temps), max(temps), mos]
    values += [int(state[k]) for k in ('battery_valid', 'mos_valid', 'communication_ok',
        'charge_request', 'discharge_request', 'output_enabled', 'afe_charge_block',
        'afe_discharge_block', 'short_active', 'physical_release')]
    return 'S ' + ' '.join(map(str, values))


def scenario_program(scenario):
    strict_keys(scenario, {'schema_version', 'name', 'parameters', 'initial', 'steps', 'tick_origin_ms'}, 'scenario')
    if type(scenario.get('schema_version')) is not int or scenario['schema_version'] != 1:
        raise ValueError('scenario schema_version must be 1')
    if not isinstance(scenario.get('name', 'unnamed'), str):
        raise ValueError('name: string required')
    state = copy.deepcopy(DEFAULT_INPUT)
    strict_keys(scenario.get('initial', {}), DEFAULT_INPUT.keys() | {'pack_mv'}, 'initial')
    state.update(scenario.get('initial', {}))
    commands = parameter_commands(scenario.get('parameters', {}))
    checks, snapshots = [], []
    origin = integer(scenario.get('tick_origin_ms', 0), 0, 0xffffffff, 'tick_origin_ms')
    steps = scenario.get('steps')
    if not isinstance(steps, list) or not steps or len(steps) > 10000:
        raise ValueError('steps: 1..10000 required')
    next_ms, sample_count = 0, 0
    for step in steps:
        strict_keys(step, {'at_ms', 'set', 'reboot', 'expect'}, 'step')
        at = integer(step.get('at_ms'), 0, 20000000, 'at_ms')
        if at % 200 or at < next_ms:
            raise ValueError('at_ms must increase on the 200ms sampling grid')
        while next_ms < at:
            commands.append(sample_command(state, origin + next_ms))
            snapshots.append({'at_ms': next_ms})
            next_ms += 200
        update = step.get('set', {})
        strict_keys(update, DEFAULT_INPUT.keys() | {'pack_mv'}, 'set')
        state.update(update)
        if 'reboot' in step and type(step['reboot']) is not bool:
            raise ValueError('reboot: boolean required')
        if step.get('reboot'):
            commands.append('R')  # validated parameters persist, volatile qualification resets
        commands.append(sample_command(state, origin + at))
        snapshots.append({'at_ms': at})
        expected = step.get('expect', {})
        strict_keys(expected, EXPECT_FIELDS, 'expect')
        for key, value in expected.items():
            integer(value, 0, 4095 if key in ('first', 'second', 'third') else 1, 'expect.' + key)
        if expected:
            checks.append((len(snapshots) - 1, expected))
        next_ms = at + 200
        sample_count = len(snapshots)
        if sample_count > 100000:
            raise ValueError('scenario exceeds 100000 samples')
    return commands, snapshots, checks


class HostSimulator:
    def __enter__(self):
        self._tmp = temp_directory()
        directory = Path(self._tmp.name)
        try:
            params_text = (APP / 'param.h').read_text(encoding='utf-8')
            match = re.search(r'struct PRT_E2ROM_PARAS\s*\{.*?\n\};', params_text, re.S)
            if not match:
                raise RuntimeError('production parameter declaration changed; update host seam')
            (directory / 'param.h').write_text('#pragma once\n#include <stdint.h>\ntypedef uint16_t u16;\n' +
                match.group(0) + '\nuint8_t bms_protection_params_valid(void);\n', encoding='utf-8')
            # Whole C implementation is copied byte-for-byte, not reimplemented.
            for original, target in [('bms_sw_protection.c', 'production_sw.c'),
                                     ('bms_sw_protection.h', 'bms_sw_protection.h')]:
                (directory / target).write_bytes((APP / original).read_bytes())
            host = Path(__file__).parent / 'host'
            for name in ('host_bridge.c', 'host_bridge.h'):
                (directory / name).write_bytes((host / name).read_bytes())
            self.executable = directory / 'bms-host.exe'
            self.compiler = compile_c([directory / 'host_bridge.c'], self.executable, [directory, APP])
            self.identity = provenance(ROOT, [APP / 'bms_sw_protection.c', APP / 'bms_sw_protection.h',
                                               APP / 'param.h', APP / 'bms_state.h', host / 'host_bridge.c',
                                               host / 'host_bridge.h', Path(__file__),
                                               Path(__file__).with_name('compiler.py'),
                                               Path(__file__).with_name('checks.py')])
            return self
        except BaseException:
            self._tmp.cleanup()
            raise

    def __exit__(self, *args):
        self._tmp.cleanup()

    def execute(self, commands):
        process = subprocess.run([str(self.executable)], input='\n'.join(commands) + '\n',
            capture_output=True, text=True, timeout=30)
        if process.returncode:
            raise RuntimeError(f'host exit={process.returncode}: {process.stderr}')
        keys = ('first', 'second', 'third', 'charge_on', 'discharge_on', 'temp_break', 'short_latched', 'accepted')
        outputs = []
        for line in process.stdout.splitlines():
            values = [int(x) for x in line.split()]
            if len(values) != len(keys):
                raise RuntimeError('host output format mismatch')
            outputs.append(dict(zip(keys, values)))
        return outputs

    def run(self, scenario):
        commands, snapshots, checks = scenario_program(scenario)
        outputs = self.execute(commands)
        if len(outputs) != len(snapshots):
            raise RuntimeError('host output sample count mismatch')
        failures = []
        for index, expected in checks:
            for key, value in expected.items():
                if outputs[index][key] != value:
                    failures.append(f"t={snapshots[index]['at_ms']} {key}: expected={value}, actual={outputs[index][key]}")
        # Unaccepted samples are always a runner failure, even without an assertion.
        failures += [f"t={snapshots[i]['at_ms']} rejected sample" for i, out in enumerate(outputs) if not out['accepted']]
        trace = [{**stamp, **out} for stamp, out in zip(snapshots, outputs)]
        return {'name': scenario.get('name', 'unnamed'), 'passed': not failures,
                'assertions': sum(len(e) for _, e in checks), 'failures': failures, 'trace': trace,
                'scenario_sha256': hashlib.sha256(json.dumps(scenario, sort_keys=True).encode()).hexdigest()}


class FakeHardwareBackend:
    """A deterministic logical HIL: same commands/reports, no DAC or real device IO.

    Charger/load detection is sampled data. It never substitutes for the
    explicit physical_release qualification supplied by an AFE backend.
    """
    def __init__(self, simulator):
        self.simulator = simulator

    def run(self, scenario):
        result = self.simulator.run(scenario)
        result['backend'] = 'fake-logical-hil'
        result['hardware_validated'] = False
        return result
