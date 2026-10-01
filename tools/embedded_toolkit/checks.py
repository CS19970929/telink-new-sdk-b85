"""Boundary scenarios and deterministic safety properties against production C."""
import copy
import random

from .simulator import DEFAULT_INPUT, PROTECTIONS, parameter_commands, sample_command

TRIPS = (3600, 2800, 3000, 2200, 100, 100, 950, 400, 950, 400, 1300, 200)
RECOVERS = (3500, 3000, 2900, 2300, 50, 50, 900, 450, 900, 450, 1200, 100)
LOW = {1, 3, 7, 9}


def measurement(index, value):
    if index in (0, 1):
        return {'cells_mv': [value] * 8}
    if index in (2, 3):
        return {'pack_mv': value * 10}
    if index in (4, 5):
        return {'current_ma': value * (100 if index == 4 else -100)}
    if index in (6, 7, 8, 9):
        return {'battery_temp_decic': [value - 400] * 2,
                'current_ma': 1000 if index in (6, 7) else -1000}
    if index == 10:
        return {'mos_temp_decic': value - 400}
    return {'cells_mv': [3300, 3300 + value] + [3300] * 6}


def scenario(name, params, steps, **extra):
    return {'schema_version': 1, 'name': name, 'parameters': params, 'steps': steps, **extra}


def regression_scenarios():
    cases = []
    for index, name in enumerate(PROTECTIONS):
        trip, recover, bit = TRIPS[index], RECOVERS[index], 1 << index
        params = {name: [trip, trip, trip, recover, 60]}
        for delta in (-1, 0, 1):
            active = trip + delta <= trip if index in LOW else trip + delta >= trip
            cases.append(scenario(name + f': threshold{delta:+d}', params, [
                {'at_ms': 0, 'set': measurement(index, trip + delta), 'expect': {'third': 0}},
                {'at_ms': 200, 'expect': {'third': 0}},
                {'at_ms': 400, 'expect': {'first': bit if active else 0,
                                         'second': bit if active else 0, 'third': bit if active else 0}}]))
        middle = (trip + recover) // 2
        cases.append(scenario(name + ': hysteresis/recovery', params, [
            {'at_ms': 0, 'set': measurement(index, trip)},
            {'at_ms': 400, 'expect': {'third': bit}},
            {'at_ms': 600, 'set': measurement(index, middle)},
            {'at_ms': 1200, 'expect': {'third': bit, 'first': 0, 'second': 0}},
            {'at_ms': 1400, 'set': measurement(index, recover), 'expect': {'third': bit}},
            {'at_ms': 1600, 'expect': {'third': bit}},
            {'at_ms': 1800, 'expect': {'third': 0}}]))
        cases.append(scenario(name + ': disabled', {name: [0, 0, 0, 0, 65535]}, [
            {'at_ms': 0, 'set': measurement(index, trip)}, {'at_ms': 2000, 'expect': {'third': 0}}]))
        cases.append(scenario(name + ': zero filter trips in one sample', {name: [trip]*3 + [recover, 0]}, [
            {'at_ms': 0, 'set': measurement(index, trip), 'expect': {'third': bit}}]))
        cases.append(scenario(name + ': maximum filter ceil and recovery', {name: [trip]*3 + [recover, 65535]}, [
            {'at_ms': 0, 'set': measurement(index, trip)},
            {'at_ms': 655000, 'expect': {'third': 0}},
            {'at_ms': 655200, 'expect': {'third': bit}},
            {'at_ms': 655400, 'set': measurement(index, recover)},
            {'at_ms': 1310400, 'expect': {'third': bit}},
            {'at_ms': 1310600, 'expect': {'third': 0}}]))
        cases.append(scenario(name + ': corrupt hysteresis blocks outputs', {name: [trip]*4 + [1]}, [
            {'at_ms': 0, 'expect': {'charge_on': 0, 'discharge_on': 0}}]))
    cases += [
        scenario('simultaneous OV/UV: clearing OV does not open DSG',
                 {'cell_ov': [3600]*3 + [3500, 20], 'cell_uv': [2800]*3 + [3000, 20]}, [
            {'at_ms': 0, 'set': {'cells_mv': [3700, 2700]+[3300]*6},
             'expect': {'third': 3, 'charge_on': 0, 'discharge_on': 0}},
            {'at_ms': 200, 'set': {'cells_mv': [3300, 2700]+[3300]*6},
             'expect': {'third': 2, 'charge_on': 0, 'discharge_on': 0}},
            {'at_ms': 400, 'expect': {'third': 2, 'charge_on': 1, 'discharge_on': 0}}]),
        scenario('leaky trip qualification preserved', {'cell_ov': [3600]*3 + [3500, 60]}, [
            {'at_ms': 0, 'set': {'cells_mv': [3600]*8}}, {'at_ms': 200},
            {'at_ms': 400, 'set': {'cells_mv': [3300]*8}},
            {'at_ms': 600, 'set': {'cells_mv': [3600]*8}, 'expect': {'third': 0}},
            {'at_ms': 800, 'expect': {'third': 1}}]),
        scenario('uint32 tick rollover', {'cell_ov': [3600]*3 + [3500, 60]}, [
            {'at_ms': 0, 'set': {'cells_mv': [3600]*8}},
            {'at_ms': 200, 'expect': {'third': 0}}, {'at_ms': 400, 'expect': {'third': 1}}],
            tick_origin_ms=0xffffffff-100),
        scenario('battery/MOS NTC failure fail-safe', {}, [
            {'at_ms': 0, 'set': {'battery_valid': False}, 'expect': {'temp_break': 1, 'charge_on': 0, 'discharge_on': 0}},
            {'at_ms': 200, 'set': {'battery_valid': True, 'mos_valid': False}, 'expect': {'temp_break': 1}},
            {'at_ms': 400, 'set': {'mos_valid': True}, 'expect': {'temp_break': 0}}]),
        scenario('short latch: zero current and comm failure cannot release', {}, [
            {'at_ms': 0, 'set': {'short_active': True}, 'expect': {'discharge_on': 0, 'short_latched': 1}},
            {'at_ms': 200, 'set': {'short_active': False, 'current_ma': 0}, 'expect': {'short_latched': 1}},
            {'at_ms': 400, 'set': {'communication_ok': False, 'physical_release': True}, 'expect': {'short_latched': 1, 'charge_on': 0}},
            {'at_ms': 600, 'set': {'communication_ok': True}},
            {'at_ms': 2200, 'expect': {'short_latched': 1}},
            {'at_ms': 2400, 'expect': {'short_latched': 0, 'discharge_on': 1}}]),
        scenario('reboot resets qualification but retains parameters', {'cell_ov': [3600]*3+[3500, 60]}, [
            {'at_ms': 0, 'set': {'cells_mv': [3600]*8}}, {'at_ms': 200},
            {'at_ms': 400, 'reboot': True, 'expect': {'third': 0}},
            {'at_ms': 600, 'expect': {'third': 0}}, {'at_ms': 800, 'expect': {'third': 1}}]),
        scenario('communication return requires three valid samples', {}, [
            {'at_ms': 0, 'expect': {'charge_on': 0, 'discharge_on': 0}},
            {'at_ms': 200, 'expect': {'charge_on': 0}},
            {'at_ms': 400, 'expect': {'charge_on': 1, 'discharge_on': 1}},
            {'at_ms': 600, 'set': {'communication_ok': False}, 'expect': {'charge_on': 0}},
            {'at_ms': 800, 'set': {'communication_ok': True}, 'expect': {'charge_on': 0}},
            {'at_ms': 1000, 'expect': {'charge_on': 0}},
            {'at_ms': 1200, 'expect': {'charge_on': 1}}]),
    ]
    for i in (6, 7, 8, 9):
        name = PROTECTIONS[i]
        cases.append(scenario(name + ': idle does not start, lost current does not clear',
                             {name: [TRIPS[i]]*3+[RECOVERS[i], 20]}, [
            {'at_ms': 0, 'set': {**measurement(i, TRIPS[i]), 'current_ma': 0}, 'expect': {'third': 0}},
            {'at_ms': 200, 'set': {'current_ma': 1000 if i in (6, 7) else -1000}, 'expect': {'third': 1 << i}},
            {'at_ms': 400, 'set': {'current_ma': 0}, 'expect': {'third': 1 << i}}]))
    return cases


def property_check(simulator, seed=20261001, samples=20000):
    randomizer = random.Random(seed)
    params = {name: [TRIPS[i]]*3 + [RECOVERS[i], 60] for i, name in enumerate(PROTECTIONS)}
    commands = parameter_commands(params)
    states = []
    origin = 0xffffffff - 1000
    for i in range(samples):
        state = copy.deepcopy(DEFAULT_INPUT)
        state['cells_mv'] = [randomizer.randrange(2500, 4100) for _ in range(8)]
        state['current_ma'] = randomizer.randrange(-25000, 25001)
        state['battery_temp_decic'] = [randomizer.randrange(-200, 900) for _ in range(2)]
        state['mos_temp_decic'] = randomizer.randrange(250, 1000)
        for key in ('communication_ok', 'mos_valid', 'battery_valid', 'output_enabled',
                    'charge_request', 'discharge_request', 'afe_charge_block', 'afe_discharge_block'):
            state[key] = randomizer.random() > 0.1
        state['short_active'] = randomizer.random() < 0.02
        state['physical_release'] = randomizer.random() < 0.3
        if randomizer.random() < 0.01:
            commands.append('R')
        commands.append(sample_command(state, origin + i * 200))
        states.append(state)
    outputs = simulator.execute(commands)
    failures = []
    if len(outputs) != samples:
        failures.append('sample count mismatch')
    for i, (state, out) in enumerate(zip(states, outputs)):
        charge_fault = out['third'] & (1 | 4 | 16 | 64 | 128 | 1024)
        discharge_fault = out['third'] & (2 | 8 | 32 | 256 | 512 | 1024)
        common_block = not state['communication_ok'] or not state['output_enabled'] or out['temp_break']
        if not out['accepted'] or (out['charge_on'] and (common_block or charge_fault or
            state['afe_charge_block'] or not state['charge_request'])) or (out['discharge_on'] and
            (common_block or discharge_fault or out['short_latched'] or state['afe_discharge_block'] or
             not state['discharge_request'])):
            failures.append(f'seed={seed} sample={i} input={state} output={out}')
            if len(failures) >= 10:
                break
    return {'name': f'safety properties seed={seed} samples={samples}', 'passed': not failures,
            'assertions': samples * 3, 'failures': failures, 'trace': [], 'seed': seed}
