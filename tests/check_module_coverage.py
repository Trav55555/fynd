#!/usr/bin/env python3
"""Require line coverage for every private C module, including included .inc files."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

FLOOR = 90


def check(payload, modules):
    if payload.get('type') != 'llvm.coverage.json.export':
        raise ValueError('not an LLVM coverage export')
    wanted = {str(path.resolve()): str(path) for path in modules}
    rows = {}
    for group in payload.get('data', []):
        for item in group.get('files', []):
            name = wanted.get(str(Path(item['filename']).resolve()))
            if name is None:
                continue
            if name in rows:
                raise ValueError('duplicate coverage entry: ' + name)
            lines = item['summary']['lines']
            count, covered = lines['count'], lines['covered']
            if not 0 <= covered <= count or count == 0:
                raise ValueError('invalid line counts: ' + name)
            percent = 100 * covered / count
            if percent < FLOOR:
                raise ValueError('%s: %.2f%% below %d%%' % (name, percent, FLOOR))
            rows[name] = {'lines': count, 'covered': covered, 'percent': percent}
    missing = set(wanted.values()) - rows.keys()
    if missing:
        raise ValueError('missing module coverage: ' + ', '.join(sorted(missing)))
    if not rows:
        raise ValueError('no modules covered')
    count = sum(row['lines'] for row in rows.values())
    covered = sum(row['covered'] for row in rows.values())
    return {'status': 'pass', 'minimum': FLOOR, 'modules': rows,
            'total': {'lines': count, 'covered': covered, 'percent': 100 * covered / count}}


def self_test():
    module = Path('src/planted.inc')
    good = {'type': 'llvm.coverage.json.export', 'data': [{'files': [
        {'filename': str(module), 'summary': {'lines': {'count': 10, 'covered': 9}}}
    ]}]}
    check(good, [module])
    for mode in ('low', 'missing', 'empty', 'duplicate'):
        broken = copy.deepcopy(good)
        rows = broken['data'][0]['files']
        if mode == 'low':
            rows[0]['summary']['lines']['covered'] = 8
        elif mode == 'missing':
            rows.clear()
        elif mode == 'empty':
            rows[0]['summary']['lines'] = {'count': 0, 'covered': 0}
        else:
            rows.append(copy.deepcopy(rows[0]))
        try:
            check(broken, [module])
        except ValueError:
            continue
        raise AssertionError('module coverage accepted broken control: ' + mode)
    print('PASS module coverage rejects four broken controls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--build', type=Path, default=Path('.dcheck/build/coverage'))
    parser.add_argument('--output', type=Path, default=Path('.dcheck/build/coverage/modules.json'))
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    modules = sorted(path for path in Path('src').iterdir() if path.suffix in {'.c', '.inc'})
    binaries = [args.build / path.stem for path in sorted(Path('tests').glob('test_*.c'))]
    try:
        if not binaries or any(not path.is_file() for path in binaries):
            raise ValueError('coverage binaries missing; run the deep checker first')
        command = ['llvm-cov', 'export', str(binaries[0])]
        for binary in binaries[1:]:
            command.extend(['-object', str(binary)])
        command.extend(['-instr-profile=' + str(args.build / 'merged.profdata'), '-summary-only'])
        command.extend(str(path) for path in modules)
        result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=30)
        payload = json.loads(result.stdout)
        summary = check(payload, modules)
        args.output.write_text(json.dumps(summary, indent=2) + '\n')
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, 'FAIL module coverage: %s\n' % error)
    for name, row in summary['modules'].items():
        print('PASS %s %.2f%% lines' % (name, row['percent']))


if __name__ == '__main__':
    main()
