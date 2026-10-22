#!/usr/bin/env python3
"""Serial, warm-cache selected-file-set benchmark. Linux + GNU time required."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time


def execute(argv, cwd, stdout, stderr, timeout):
    start = time.perf_counter_ns()
    with open(stdout, 'wb') as out, open(stderr, 'wb') as err:
        # A pipe lets communicate wait for EOF instead of wait(timeout)'s polling sleeps.
        proc = subprocess.Popen(argv, cwd=cwd, stdout=out, stderr=subprocess.PIPE,
                                start_new_session=True, env=dict(os.environ, LC_ALL='C'))
        try:
            try:
                _, errors = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                _, errors = proc.communicate()
                err.write(errors)
                raise RuntimeError(f'timeout after {timeout}s: {argv!r}')
            err.write(errors)
            status = proc.returncode
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
    elapsed = time.perf_counter_ns() - start
    if status:
        raise RuntimeError(f'exit {status}: {argv!r}; see {stderr}')
    if Path(stderr).read_bytes():
        raise RuntimeError(f'unexpected stderr: {argv!r}; see {stderr}')
    return elapsed


def validate(path, expected):
    data = Path(path).read_bytes()
    if data and not data.endswith(b'\0'):
        raise RuntimeError(f'truncated/non-NUL output: {path}')
    records = data[:-1].split(b'\0') if data else []
    if Counter(records) != Counter(expected):
        raise RuntimeError(f'wrong/missing/duplicate selected files: {path}')


def fixture(base, quick):
    params = dict(wide=200 if quick else 20000, depth=12 if quick else 96,
                  per_level=8, fanout=3, levels=3 if quick else 5)
    manifests = {}
    for shape in ('wide', 'deep', 'mixed'):
        root = base / shape
        root.mkdir()
        paths = []
        def populate(directory, count):
            for i in range(count):
                name = ('.' if i % 7 == 0 else '') + ('needle' if i % 11 == 0 else 'file') + f'-{i:05d}'
                path = directory / name
                path.touch()
                paths.append(os.fsencode(str(path.relative_to(base))))
        if shape == 'wide':
            populate(root, params['wide'])
        elif shape == 'deep':
            current = root
            for level in range(params['depth']):
                current = current / f'd{level:02d}'
                current.mkdir()
                populate(current, params['per_level'])
        else:
            frontier = [root]
            for level in range(params['levels']):
                following = []
                for directory in frontier:
                    populate(directory, 12)
                    for i in range(params['fanout']):
                        child = directory / (('.hidden' if i == 0 else 'dir') + str(i))
                        child.mkdir()
                        following.append(child)
                frontier = following
        manifests[shape] = paths
    return params, manifests


def resolve(value, required=True):
    found = shutil.which(value)
    if not found:
        if required:
            raise RuntimeError(f'required executable not found: {value}; supply its CLI path')
        return None
    return str(Path(found).resolve())


def identity(path, flag, output, timeout):
    stem = output / ('version-' + hashlib.sha256(path.encode()).hexdigest()[:12])
    execute([path, flag], output, str(stem) + '.out', str(stem) + '.err', timeout)
    version = Path(str(stem) + '.out').read_text(errors='replace').strip()
    return dict(path=path, sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(), version=version)


def commands(tools, shape, pattern, threads=1, defaults=False):
    """defaults=True omits every thread option, so each tool uses its own default."""
    fynd_jobs = [] if defaults else ['-j' + str(threads)]
    fd_jobs = [] if defaults else ['--threads', '1']
    result = {
        'fynd': [tools['fynd'], '--no-ignore', *fynd_jobs, '-0', '-H', '-t', 'f', pattern, shape],
        'find': [tools['find'], shape, '-type', 'f', '-name', '*' + pattern + '*', '-print0'],
    }
    if tools['fd']:
        result['fd'] = [tools['fd'], '-0', '-H', '-I', '--no-ignore-parent',
                        '--case-sensitive', '--fixed-strings', *fd_jobs,
                        '--color', 'never', '-t', 'f', pattern, shape]
    return result


def run(args, output):
    tools = {name: resolve(getattr(args, name), name != 'fd') for name in ('fynd', 'fd', 'find', 'time')}
    metadata = {}
    for name, path in tools.items():
        if path:
            metadata[name] = identity(path, '--version', output, args.timeout)
        else:
            metadata[name] = dict(status='SKIP', reason=f'optional executable not found: {args.fd}')
    if 'GNU' not in metadata['time']['version'] or 'GNU' not in metadata['find']['version']:
        raise RuntimeError('GNU find and GNU /usr/bin/time required; check --find and --time')
    cpu = 'unknown'
    if Path('/proc/cpuinfo').exists():
        cpu = next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                    if line.startswith('model name')), cpu)
    report = dict(status='RUNNING', tools=metadata, os=platform.platform(), cpu=cpu, locale='C',
                  python=sys.version, seed=0, cache_policy='WARM: creation and all-tool validation preheat; no cache dropping',
                  semantics='selected-file-set equality (including multiplicity), not ordered traversal parity',
                  metric='perf_counter_ns end-to-end: process startup, GNU time wrapper and output-file I/O included; not pure walk',
                  rss_unit='KiB (GNU time %M)', samples=[], validation=[], order=[])
    # Retain provenance even if fixture generation or later validation fails.
    (output / 'metadata.json').write_text(json.dumps(report, indent=2) + '\n')
    with tempfile.TemporaryDirectory(prefix='fynd-bench-fixture-') as directory:
        base = Path(directory)
        report['fixture_cwd'] = str(base)
        report['requested'] = dict(fynd=args.fynd, fd=args.fd, find=args.find, time=args.time,
                                   repeats=args.repeats, quick=args.quick, timeout=args.timeout, fynd_threads=args.fynd_threads,
                                   defaults=args.defaults)
        params, manifests = fixture(base, args.quick)
        report['fixture'] = dict(parameters=params, shapes={
            shape: dict(files=len(paths), directories=sum(1 for _ in os.walk(base / shape)), bytes_per_file=0, needle_files=sum(b'needle' in p.rsplit(b'/', 1)[-1] for p in paths),
                        manifest_sha256=hashlib.sha256(b'\0'.join(sorted(paths)) + b'\0').hexdigest())
            for shape, paths in manifests.items()})
        jobs = []
        groups = []
        for shape, paths in manifests.items():
            for query, pattern in (('all', ''), ('needle', 'needle')):
                expected = [p for p in paths if pattern.encode() in p.rsplit(b'/', 1)[-1]]
                group = [(f'{shape}-{query}-{tool}', argv, expected)
                         for tool, argv in commands(tools, shape, pattern, args.fynd_threads,
                                                           args.defaults).items()]
                jobs.extend(group)
                groups.append(group)
        (output / 'metadata.json').write_text(json.dumps(report, indent=2) + '\n')
        # Strict global validation barrier: no measured invocation until every tool/query passes.
        for label, argv, expected in jobs:
            out, err = output / (label + '-check.out'), output / (label + '-check.err')
            execute(argv, base, out, err, args.timeout)
            validate(out, expected)
            report['validation'].append(dict(job=label, argv=argv))
        for repeat in range(args.repeats):
            ordered = []
            for group in groups[repeat % len(groups):] + groups[:repeat % len(groups)]:
                offset = repeat % len(group)
                ordered.extend(group[offset:] + group[:offset])
            report['order'].append([job[0] for job in ordered])
            for label, argv, expected in ordered:
                prefix = output / f'{label}-{repeat}'
                rss = Path(str(prefix) + '.rss')
                wrapped = [tools['time'], '-f', '%M', '-o', str(rss), '--'] + argv
                elapsed = execute(wrapped, base, str(prefix) + '.out', str(prefix) + '.err', args.timeout)
                validate(str(prefix) + '.out', expected)
                raw = rss.read_text()
                if not raw.endswith('\n') or not raw[:-1].isdigit():
                    raise RuntimeError(f'invalid GNU time RSS: {rss}')
                report['samples'].append(dict(job=label, repeat=repeat, argv=argv, measured_argv=wrapped,
                                              elapsed_ns=elapsed, peak_rss_kib=int(raw)))
        report['medians'] = {label: {
            field: statistics.median(s[field] for s in report['samples'] if s['job'] == label)
            for field in ('elapsed_ns', 'peak_rss_kib')} for label, _, _ in jobs}
    for name, path in tools.items():
        if path and hashlib.sha256(Path(path).read_bytes()).hexdigest() != metadata[name]['sha256']:
            raise RuntimeError(f'executable changed during benchmark: {path}')
    report['status'] = 'SUCCESS'
    return report


def positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return number


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fynd', required=True)
    parser.add_argument('--fd', default='fd')
    parser.add_argument('--find', default='/usr/bin/find')
    parser.add_argument('--time', default='/usr/bin/time')
    parser.add_argument('--output', type=Path, default=Path('build/bench'), help='parent for a unique run directory')
    parser.add_argument('--repeats', type=positive, default=5)
    parser.add_argument('--fynd-threads', type=int, choices=range(1, 33), default=1)
    parser.add_argument('--defaults', action='store_true',
                        help='omit thread options so fynd and fd use their own defaults')
    parser.add_argument('--timeout', type=positive, default=120, help='seconds per invocation')
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output)).resolve()
    print(output, flush=True)
    try:
        if sys.platform != 'linux':
            raise RuntimeError('Linux only initially')
        report = run(args, output)
    except Exception as error:
        (output / 'failure.json').write_text(json.dumps(dict(status='FAILED', error=str(error)), indent=2) + '\n')
        print(f'benchmark rejected: {error}', file=sys.stderr)
        return 1
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
