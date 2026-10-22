#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dcheck="${DCHECK:-$HOME/projects/dependable-c-agent-toolchain/bin/dcheck}"
case "${1:-}" in
    --scoped)
        tools="$(dirname "$(readlink -f "$dcheck")")/dcheck-tools"
        exec "$tools" exec bash "$root/tests/verify.sh" --profiled ;;
    --profiled) ;;
    *)
        exec systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
            nice -n 15 bash "$root/tests/verify.sh" --scoped ;;
esac
ulimit -c 0
cd "$root"
dep="$(cd "${CUTILS:-../c-utils}" && pwd)"
base="${BUILD:-build}/verification"
mkdir -p "$base"
receipt="$(mktemp -d "$base/run.XXXXXX")"
receipt="$(cd "$receipt" && pwd)"
printf 'Verification artifacts: %s\n' "$receipt"
trap 'status=$?; printf "%s\n" "$status" > "$receipt/exit"; if (( status != 0 )); then printf "FAIL fynd acceptance. Inspect %s\n" "$receipt" >&2; fi' EXIT
python3 - "$root" "$dep" "$receipt" <<'PY'
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
root, dep, receipt = map(Path, sys.argv[1:])
revision = (root / 'CUTILS_REVISION').read_text().strip()
if not re.fullmatch('[0-9a-f]{40}', revision):
    raise SystemExit('CUTILS_REVISION must contain a complete commit ID')
resolved = subprocess.check_output(['git', '-C', str(dep), 'rev-parse', revision + '^{commit}'], text=True).strip()
if resolved != revision:
    raise SystemExit('dependency revision mismatch')
source = receipt / 'source'
app = source / 'fynd'
library = source / 'c-utils'
app.mkdir(parents=True)
library.mkdir()
for name in ['src', 'include', 'cmd', 'tests', 'fuzz', 'semantics', 'bench', 'ci']:
    shutil.copytree(root / name, app / name, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
for name in ['Makefile', 'dcheck.conf', 'CUTILS_REVISION', 'README.md']:
    shutil.copy2(root / name, app / name)
build_guard = ['Makefile', 'CUTILS_REVISION', 'ci/pinned_cutils.py',
               'ci/pinned_cutils.json', 'tests/test_build.py', 'tests/verify.sh']
if not all((app / name).is_file() for name in build_guard):
    raise SystemExit('missing required native build guard input')
# Bind receipts to the current inputs, not the retired sort/listing fixtures.
# test_cursor.c is discovered automatically when supplied alongside the five
# current suites; make, sensitivity and coverage must use that same full list.
c_tests = sorted(str(path.relative_to(app)) for path in (app / 'tests').glob('test*.c'))
required_c = {'tests/test_fynd.c', 'tests/test_ignore.c', 'tests/test_sched.c',
              'tests/test_stream.c'}
retired_c = {'tests/test_sort.c', 'tests/test_listing.c', 'tests/test_listing_worker.c',
             'tests/test_parallel.c', 'tests/test_batches.c'}
if not required_c.issubset(c_tests) or retired_c.intersection(c_tests):
    raise SystemExit('missing active C suite or stale retired sorting/listing suite')
make_tests = sorted(str(path.relative_to(app)) for path in (app / 'tests').glob('test_*.c'))
if c_tests != make_tests:
    raise SystemExit('make and qualification C target discovery disagree')
semantics = sorted(str(path.relative_to(app)) for path in (app / 'semantics').glob('sem_*.c'))
fuzz = sorted(str(path.relative_to(app)) for path in (app / 'fuzz').glob('fuzz_*.c'))
if (len(semantics) != 4 or 'semantics/sem_stream.c' not in semantics or
        'semantics/sem_sort.c' in semantics):
    raise SystemExit('require four semantics targets including sem_stream, not sem_sort')
if len(fuzz) != 4 or 'fuzz/fuzz_stream.c' not in fuzz or 'fuzz/fuzz_sort.c' in fuzz:
    raise SystemExit('require four fuzz targets including fuzz_stream, not fuzz_sort')
helpers = sorted(str(path.relative_to(app)) for path in (app / 'tests').glob('*.py'))
if 'tests/output_oracle.py' not in helpers:
    raise SystemExit('missing byte-exact unordered output helper')
modules = sorted(str(path.relative_to(app)) for path in (app / 'src').iterdir()
                 if path.suffix in {'.c', '.inc'})
if not {'src/fynd.c', 'src/ignore.inc', 'src/listing.inc', 'src/sched.inc'}.issubset(modules):
    raise SystemExit('missing required source module')
(receipt / 'qualification-inputs.json').write_text(json.dumps({
    'c_tests': c_tests, 'semantics': semantics, 'fuzz': fuzz,
    'source_modules': modules, 'python_tools_and_helpers': helpers,
    'build_guard_inputs': build_guard,
    'coverage_minimum': 90,
    'contract': 'unordered within roots; retain partial output on late ordinary EIO',
}, indent=2) + '\n')
archive = subprocess.Popen(['git', '-C', str(dep), 'archive', revision], stdout=subprocess.PIPE)
try:
    extracted = subprocess.run(['tar', '-x', '-C', str(library)], stdin=archive.stdout)
finally:
    archive.stdout.close()
if archive.wait() or extracted.returncode:
    raise SystemExit('cannot extract pinned dependency')
manifest = {}
for repository in [app, library]:
    manifest[repository.name] = {
        str(path.relative_to(repository)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(repository.rglob('*')) if path.is_file()
    }
(receipt / 'source-hashes.json').write_text(json.dumps(manifest, indent=2) + '\n')
(receipt / 'dependency-revision').write_text(revision + '\n')
PY
cd "$receipt/source/fynd"
python3 -I tests/test_build.py --cutils ../c-utils
python3 -I tests/check_receipt.py --self-test
python3 -I tests/check_module_coverage.py --self-test
make bench-test
make -j2 CUTILS=../c-utils BUILD=build/native all test
python3 -I tests/test_cli.py build/native/fynd --cases 500 --seed 1
python3 -I tests/test_cli.py build/native/fynd --cases 500 --seed 7
python3 -I tests/test_cli.py build/native/fynd --cases 500 --seed 42
python3 -I tests/test_ignore_cli.py build/native/fynd --cases 64 --seed 47
python3 -I tests/test_ignore_git.py build/native/fynd --cases 1000 --seed 1000
"$dcheck" fast cmd/main.c --json > "$receipt/cli.json"
"$dcheck" deep --json > "$receipt/deep.json"
python3 -I tests/check_receipt.py "$receipt/deep.json"
python3 -I tests/check_module_coverage.py --output "$receipt/modules.json"
python3 -I tests/sensitivity.py --cutils "$receipt/source/c-utils" --build-root "$receipt/sensitivity" --cases 100
printf 'PASS fynd acceptance\n'
