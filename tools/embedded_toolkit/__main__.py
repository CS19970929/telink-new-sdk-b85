"""Run from repo root: python -B -m tools.embedded_toolkit --help."""
import argparse
import json
from pathlib import Path
import sys

from .common import default_output, write_json


def main():
    parser = argparse.ArgumentParser(description='Offline Embedded Toolkit: production-C host / fake HIL / repository context')
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('simulate', 'hil'):
        p = sub.add_parser(name)
        p.add_argument('scenario', type=Path)
        p.add_argument('--output', type=Path, default=default_output(name))
    p = sub.add_parser('test')
    p.add_argument('--seed', type=int, default=20261001)
    p.add_argument('--samples', type=int, default=20000)
    p.add_argument('--output', type=Path, default=default_output('tests'))
    p = sub.add_parser('scan')
    p.add_argument('root', type=Path)
    p.add_argument('--source-order', type=Path)
    p.add_argument('--source-root', type=Path)
    p.add_argument('--compile-commands', type=Path)
    p.add_argument('--output', type=Path, default=default_output('context'))
    p = sub.add_parser('diff')
    p.add_argument('before')
    p.add_argument('after')
    p.add_argument('--root', type=Path, default=Path.cwd())
    p.add_argument('--output', type=Path, default=default_output('diff'))
    args = parser.parse_args()
    if args.command == 'scan':
        from .scanner import scan, write_scan
        data = scan(args.root, args.source_order, args.source_root, args.compile_commands)
        write_scan(args.output, data)
        print(json.dumps(data['summary'], ensure_ascii=False))
    elif args.command == 'diff':
        from .scanner import diff
        write_json(args.output / 'change_context.json', diff(args.root, args.before, args.after))
    else:
        from .simulator import FakeHardwareBackend, HostSimulator, load_scenario
        from .reports import write_reports
        with HostSimulator() as simulator:
            if args.command == 'test':
                if not 1 <= args.samples <= 100000:
                    raise ValueError('--samples must be 1..100000')
                from .checks import property_check, regression_scenarios
                results = [simulator.run(s) for s in regression_scenarios()]
                results.append(property_check(simulator, args.seed, args.samples))
            else:
                backend = FakeHardwareBackend(simulator) if args.command == 'hil' else simulator
                results = [backend.run(load_scenario(args.scenario))]
            summary = write_reports(args.output, results, simulator.identity, simulator.compiler)
        print(f"cases={summary['cases']} failed={summary['failed']}")
        print(str(args.output / 'report.html'))
        return 1 if summary['failed'] else 0
    print(str(args.output))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, OSError) as exc:
        print('ERROR: ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
