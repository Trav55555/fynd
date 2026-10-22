#!/usr/bin/env python3
"""Reject incomplete or failed dcheck deep receipts."""
import argparse
import copy
import json
from pathlib import Path

REQUIRED = {
    'subset', 'lint', 'matrix', 'analyzer', 'sanitize', 'msan', 'tsan',
    'release', 'wasm', 'i386', 'coverage', 'semantics', 'valgrind', 'fuzz',
}
OPTIONAL = {'authority', 'regions', 'cbmc'}


def check(result):
    if (result.get('schema') != 'dcheck.result/0' or result.get('tier') != 'deep'
            or result.get('status') != 'pass' or result.get('failed') != []):
        raise ValueError('deep receipt is not a successful result')
    checks = result.get('checks')
    if not isinstance(checks, list):
        raise ValueError('missing checks')
    names = [item.get('name') for item in checks]
    if len(set(names)) != len(names):
        raise ValueError('duplicate checks')
    passed = {item['name'] for item in checks if item.get('status') == 'pass'}
    skipped = {item['name'] for item in checks if item.get('status') == 'skip'}
    if REQUIRED - passed:
        raise ValueError('required checks did not pass: ' + ', '.join(sorted(REQUIRED - passed)))
    if skipped - OPTIONAL or set(result.get('skipped', [])) != skipped:
        raise ValueError('unexpected or inconsistent skips')
    if any(item.get('status') not in {'pass', 'skip'} for item in checks):
        raise ValueError('failed or unknown check status')
    for item in checks:
        if item.get('status') == 'skip' and not item.get('detail'):
            raise ValueError('skip lacks an explicit reason')


def self_test():
    good = {'schema': 'dcheck.result/0', 'tier': 'deep', 'status': 'pass',
            'failed': [], 'skipped': [],
            'checks': [{'name': n, 'status': 'pass'} for n in sorted(REQUIRED)]}
    check(good)
    controls = []
    bad = copy.deepcopy(good)
    bad['status'] = 'fail'
    controls.append(bad)
    bad = copy.deepcopy(good)
    bad['checks'] = [c for c in bad['checks'] if c['name'] != 'sanitize']
    controls.append(bad)
    bad = copy.deepcopy(good)
    bad['checks'][0]['status'] = 'skip'
    bad['checks'][0]['detail'] = 'planted required skip'
    bad['skipped'] = [bad['checks'][0]['name']]
    controls.append(bad)
    bad = copy.deepcopy(good)
    bad['checks'].append(bad['checks'][0])
    controls.append(bad)
    for bad in controls:
        try:
            check(bad)
        except ValueError:
            continue
        raise AssertionError('receipt checker accepted a broken control')
    print('PASS receipt checker rejects four broken controls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipt', nargs='?', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.receipt is not None:
        try:
            check(json.loads(args.receipt.read_text()))
        except (ValueError, KeyError, TypeError) as error:
            parser.exit(1, f'FAIL receipt: {error}\n')
        print('PASS required deep checks')
    elif not args.self_test:
        parser.error('provide a receipt or --self-test')


if __name__ == '__main__':
    main()
