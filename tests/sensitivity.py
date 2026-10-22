#!/usr/bin/env python3
"""Small behavioral sensitivity check; retained scratch trees contain logs/summary.json."""
import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import tempfile


MUTATION_FILES = ('src/fynd.c', 'src/ignore.inc', 'src/listing.inc', 'src/sched.inc')
SOURCE = Path(__file__).resolve().parent.parent
RETIRED_C_TESTS = {'test_sort.c', 'test_listing.c', 'test_listing_worker.c',
                   'test_parallel.c', 'test_batches.c'}
C_TESTS = tuple(str(path.relative_to(SOURCE))
                for path in sorted((SOURCE / 'tests').glob('test*.c'))
                if path.name not in RETIRED_C_TESTS)
# Copy every current C fixture (including test_cursor.c when present), private
# module and Python tool/helper. Compile and execute the SAME discovered list.
FILES = tuple(sorted(set(MUTATION_FILES + ('include/fynd.h', 'cmd/main.c') + C_TESTS)
                     | {str(path.relative_to(SOURCE))
                        for pattern in ('src/*.c', 'src/*.inc', 'src/*.h',
                                        'include/**/*.h', 'tests/**/*.py',
                                        'tests/**/*.h', 'fuzz/**/*.h')
                        for path in SOURCE.glob(pattern)}))
EXPECTED_MUTATIONS = 33  # 19 retained + 6 scheduler + frame elision + hold release + ** collapse + sort
                         # + two dir-relative probe controls + two .git file controls.
STRICT = ['-std=c89', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
          '-Wconversion', '-Wshadow', '-Wstrict-prototypes', '-Wmissing-prototypes']
MUTATIONS = (
    ('suppress-name-filter', 'src/fynd.c',
     'if (!contains(name, q->pattern, q->fold))',
     'if (!contains(name, q->pattern, q->fold) && 0)',
     'literal name and no-match checks'),
    ('ignore-type-filter', 'src/fynd.c',
     'if (q->kinds != 0 && (q->kinds & (1U << (unsigned int)kind)) == 0)',
     'if (q->kinds != 0 && (q->kinds & (1U << (unsigned int)kind)) == 0 && 0)',
     'type union and FIFO selection checks'),
    ('include-hidden', 'src/fynd.c',
     "    return !r->query->hidden && name.ptr[0] == '.';",
     "    return !r->query->hidden && name.ptr[0] == '.' && 0;",
     'default hidden pruning checks'),
    ('follow-root-links', 'src/fynd.c',
     'if (lstat(path, &st) != 0)',
     'if (stat(path, &st) != 0)',
     'explicit and broken root link checks'),
    ('ignore-delayed-flush-failure', 'src/fynd.c',
     'if (fflush(out) == EOF || (r.status != 2 && ferror(out)))',
     'if ((fflush(out) == EOF || (r.status != 2 && ferror(out))) && 0)',
     'C buffered output failure checks'),
    ('raw-unescaped-diagnostics', 'src/fynd.c',
     "if (*p >= 32 && *p <= 126 && *p != '\\\\' && *p != '\\'') {",
     'if (1) {',
     'literal exact diagnostic escaping check'),
    ('terminal-listing-eio', 'src/listing.inc',
     'return error == ENOMEM || error == E2BIG || error == EMFILE || error == ENFILE;',
     'return error == ENOMEM || error == E2BIG || error == EMFILE || error == ENFILE || error == EIO;',
     'retained late-EIO partial output, nonterminal status and independent-root continuation; '
     'test_sched worker late EIO stays nonterminal with serial-equal output'),
    ('truncate-joined-path-1023', 'src/fynd.c',
     '    path[n + name.len] = 0;',
     '    path[n + name.len] = 0;\n'
     '    if (n + name.len > 1023)\n'
     '        path[1023] = 0;',
     'exact long joined paths and subtree selection checks'),
    ('disable-ignore-default', 'src/fynd.c',
     'q->no_ignore = 0;',
     'q->no_ignore = 1;',
     'default ignore discovery and precedence checks'),
    ('bypass-ignore-pruning', 'src/fynd.c',
     'else if (verdict != 1)',
     'else if (verdict != 2)',
     'ignored directory pruning and ignored file exclusion checks'),
    ('suppress-thread-start', 'src/fynd.c',
     'if (!r.stop && threads > 1)',
     'if (!r.stop && threads > 32)',
     'test_sched: forced primitive-init failure must report the requested workers'),
    ('serial-default', 'src/fynd.c',
     '    online = FYND_ONLINE_CPUS();',
     '    online = 1L;',
     'test_sched: without -j the online CPU count must start workers'),
    ('drop-worker-settle-flush', 'src/sched.inc',
     '    ok = sched_flush(w);\n    cu_arena_release(&w->arena);',
     '    ok = 1;\n    w->used = 0;\n    cu_arena_release(&w->arena);',
     'test_sched equivalence: records buffered at the root barrier must be written'),
    ('drop-coordinator-settle', 'src/sched.inc',
     '    ok = sched_settle(w);\n    cu_mutex_lock(s->mu);\n    while (s->acked != s->started)',
     '    ok = 1;\n    w->used = 0;\n    cu_mutex_lock(s->mu);\n    while (s->acked != s->started)',
     'test_sched equivalence: worker 0 records must be written at the barrier'),
    ('suppress-output-failure-report', 'src/sched.inc',
     '    if (first)\n        (void)diagnostic(r, "output failure", NULL);',
     '    if (first && 0)\n        (void)diagnostic(r, "output failure", NULL);',
     'test_sched exact /dev/full diagnostics: one output failure report'),
    ('keep-empty-ignore-frames', 'src/ignore.inc',
     '    if (k == IGNORE_CLASSES && !f->repository &&',
     '    if (k == IGNORE_CLASSES && !f->repository && 0 &&',
     'test_ignore elision: ruleless, non-repository directories reuse the parent frame'),
    ('skip-gitignore-probe', 'src/ignore.inc',
     '        if (k == IGNORE_GIT && !repository) continue;',
     '        if (k == IGNORE_GIT) continue;',
     'ignore CLI and Git differential require .gitignore below a repository root'),
    ('ignore-probe-absence', 'src/ignore.inc',
     '    case CU_INFO_MISSING: *present = 0; return 1;',
     '    case CU_INFO_MISSING: *present = 1; return 1;',
     'test_ignore elision: a directory without sources must reuse the parent frame'),
    ('ignore-gitfile', 'src/ignore.inc',
     '    if (S_ISREG(st.st_mode)) {',
     '    if (S_ISREG(st.st_mode) && 0) {',
     'test_ignore requires a .git file\'s gitdir for info/exclude'),
    ('ignore-commondir', 'src/ignore.inc',
     '    if (text.ptr != NULL) {\n        git = ignore_git_path(a, git, text);',
     '    if (text.ptr != NULL && 0) {\n        git = ignore_git_path(a, git, text);',
     'test_ignore requires commondir for linked worktrees'),
    ('retain-visit-hold', 'src/fynd.c',
     '    if (own != NULL)\n        sched_drop(r->sched, own);',
     '    if (own != NULL && 0)\n        sched_drop(r->sched, own);',
     'test_sched hold release: a finished directory must drop its frame hold'),
    ('lose-settle-failure', 'src/sched.inc',
     '    report = s->settle_failed && !s->output_reported;',
     '    report = 0;',
     'test_sched barrier write failure must be reported once'),
    ('disable-necessary-start-filter', 'src/ignore.inc',
     "        if (r->len != 0 && r->glob[0] != '*' &&",
     "        if (r->len != 0 && 0 && r->glob[0] != '*' &&",
     'charged-work sensor rejects repeated glob/suffix scans for impossible rules'),
    ('skip-sort', 'src/fynd.c',
     '    qsort(CU_VEC_DATA(&entries, SortedEntry), cu_vec_len(&entries), sizeof(SortedEntry),',
     '    if (0) qsort(CU_VEC_DATA(&entries, SortedEntry), cu_vec_len(&entries), sizeof(SortedEntry),',
     'test_cli --sort output must equal the oracle\'s exact byte-sorted preorder'),
    ('expand-repeated-doublestar', 'src/ignore.inc',
     "            while (gl >= 5 && g[3] == '*' && g[4] == '*' && (gl == 5 || g[5] == '/')) {",
     "            while (gl >= 5 && g[3] == '*' && g[4] == '*' && (gl == 5 || g[5] == '/') && 0) {",
     'test_ignore work bound: repeated ** on a deep miss must stay linear'),
    ('invert-necessary-start-filter', 'src/ignore.inc',
     '            } else if (base_len == 0 || first != (unsigned char)base[0]) {',
     '            } else if (base_len == 0 || first == (unsigned char)base[0]) {',
     'literal/escape/high-byte/last-match oracle rejects false-negative rejection'),
    ('skip-frames-without-plain-rules', 'src/ignore.inc',
     '        rel = f->files[IGNORE_PLAIN].count == 0 && f->files[IGNORE_GIT].count == 0 &&\n'
     '              f->files[IGNORE_EXCLUDE].count == 0 ? NULL :',
     '        rel = f->files[IGNORE_PLAIN].count == 0 ? NULL :',
     'ignore CLI oracle rejects dropping frames that hold only Git-class rules'),
    ('suppress-name-enomem-normalization', 'src/listing.inc',
     '    next = cu_dir_next(&job->dir, entry);\n'
     '    if (cu_arena_failed(&job->arena))',
     '    next = cu_dir_next(&job->dir, entry);\n'
     '    if (cu_arena_failed(&job->arena) && 0)',
     'test_stream late_errors: sticky name-copy ENOMEM must be terminal, '
     'diagnosed as allocation failure, and prevent later explicit roots'),
    ('omit-old-entry-rewind', 'src/listing.inc',
     '    /* End the OLD name scope, not the scope that will own the new name. */\n'
     '    if (job->active) {',
     '    /* End the OLD name scope, not the scope that will own the new name. */\n'
     '    if (job->active && 0) {',
     'test_stream quiet_name_bound: 5000 quiet names must stay within the '
     '65536-byte arena budget independently of directory width'),
    ('leak-open-cursor-on-release', 'src/listing.inc',
     '    if (job->open) {',
     '    if (job->open && 0) {',
     'test_stream policy-error/early-stop/EMFILE cleanup must close every '
     'successful open before returning, including an unread cursor'),
    ('skip-anchor-enomem-branch', 'src/listing.inc',
     '    if (cu_arena_push_bytes(&job->arena, 1) == NULL) {',
     '    if (cu_arena_push_bytes(&job->arena, 1) == NULL && job->path[0] == 0) {',
     'test_stream cursor_anchor_failure: a real failed anchor allocation '
     'must close without any next call or later-root continuation'),
    ('suppress-ordinary-read-error', 'src/listing.inc',
     '    job->open = 0;\n'
     '    job->state = job->error != 0 ? -1 : 2;\n'
     '    return job->error != 0 ? -1 : 0;',
     '    job->open = 0;\n'
     '    if (!listing_resource(job->error))\n'
     '        job->error = 0;\n'
     '    job->state = job->error != 0 ? -1 : 2;\n'
     '    return job->error != 0 ? -1 : 0;',
     'test_fynd/test_stream late EIO and test_sched worker late EIO '
     'require failure status and escaped diagnostics, not successful EOF'),
    ('skip-current-policy-before-enumeration', 'src/fynd.c',
     '        } else if (!ignore_stream_enter(r->arena, rules, &job.dir, path, &rules, &error)) {',
     '        } else if (depth < 0 &&\n'
     '                   !ignore_stream_enter(r->arena, rules, &job.dir, path, &rules, &error)) {',
     'test_stream policy_before_any_next, late_ignore_policy and dangling_repository_marker '
     'require current-directory rules before the first child; ancestor seeding remains live'),
)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(command, tree, label, timeout):
    stdout = tree / (label + '.stdout')
    stderr = tree / (label + '.stderr')
    result = {'command': command, 'cwd': str(tree), 'stdout': str(stdout),
              'stderr': str(stderr), 'returncode': None, 'timeout': False}
    temporary = tree / 'tmp'
    env = os.environ.copy()
    env['TMPDIR'] = str(temporary)
    try:
        with stdout.open('wb') as out, stderr.open('wb') as err:
            proc = subprocess.Popen(command, cwd=tree, stdout=out, stderr=err,
                                    start_new_session=True, preexec_fn=no_core,
                                    env=env)
            try:
                result['returncode'] = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                result['timeout'] = True
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        # Both CLI oracles have per-invocation timeouts. Neither is a kill.
        errors = stderr.read_bytes()
        if b'TimeoutExpired' in errors or b'10 second timeout' in errors:
            result['timeout'] = True
    except OSError as error:
        result['error'] = str(error)
    finally:
        # Only this freshly owned child directory is disposable. Keep logs
        # outside it, including when an assertion aborts before fixture cleanup.
        if temporary.exists():
            shutil.rmtree(temporary)
        temporary.mkdir()
    return result


def gate(tree, cutils, cases):
    common = ['cc', '-D_POSIX_C_SOURCE=200809L', '-D_DEFAULT_SOURCE']
    common += STRICT + ['-O2', '-g', '-Iinclude', '-I' + str(cutils / 'include')]
    libraries = [str(p) for p in sorted((cutils / 'src').glob('*.c'))]
    commands = [common + ['cmd/main.c', 'src/fynd.c'] + libraries
                + ['-pthread', '-o', 'build/fynd']]
    commands += [common + [test] + libraries
                 + ['-pthread', '-o', 'build/' + Path(test).stem]
                 for test in C_TESTS]
    results = []
    for index, command in enumerate(commands):
        result = run(command, tree, 'compile-%d' % index, 120)
        results.append(result)
        if result['timeout'] or result['returncode'] != 0:
            return ('timeout' if result['timeout'] else 'compile-failed'), results
    checks = [(Path(test).stem + '-c-tests',
               [str(tree / 'build' / Path(test).stem)]) for test in C_TESTS]
    checks += [
        ('cli-tests', [sys.executable, '-I', 'tests/test_cli.py',
                       str(tree / 'build/fynd'), '--cases', str(cases),
                       '--seed', '1']),
        ('ignore-cli-tests', [sys.executable, '-I', 'tests/test_ignore_cli.py',
                              str(tree / 'build/fynd'), '--cases', '16',
                              '--seed', '47'])]
    for label, command in checks:
        result = run(command, tree, label, 90)
        results.append(result)
        # Stop only after recording the command. Timeouts are inconclusive,
        # even if the oracle also exits nonzero with an assertion failure.
        if result['timeout']:
            return 'timeout', results
        if 'error' in result:
            return 'execution-failed', results
        if result['returncode'] != 0:
            return 'rejected', results
    return 'passed', results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cutils', required=True, type=Path)
    parser.add_argument('--build-root', type=Path,
                        help='parent for a new uniquely owned, retained run directory')
    parser.add_argument('--cases', type=int, default=100)
    args = parser.parse_args()
    if args.cases < 100:
        parser.error('--cases must be at least 100 (full 508-check CLI baseline)')
    required_tests = {'tests/test_fynd.c', 'tests/test_ignore.c',
                      'tests/test_sched.c', 'tests/test_stream.c'}
    if not required_tests.issubset(C_TESTS):
        parser.error('missing required active C fixtures')
    if (len(MUTATIONS) != EXPECTED_MUTATIONS or
            len({mutation[0] for mutation in MUTATIONS}) != EXPECTED_MUTATIONS):
        parser.error('control inventory must contain exactly %d unique active mutations' % EXPECTED_MUTATIONS)
    cutils = args.cutils.resolve()
    if not (cutils / 'include/cu.h').is_file() or not list((cutils / 'src').glob('*.c')):
        parser.error('--cutils must contain include/cu.h and src/*.c')
    parent = (args.build_root or Path(tempfile.gettempdir())).resolve()
    if parent == cutils or cutils in parent.parents:
        parser.error('--build-root must not be inside the read-only cutils tree')
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='fynd-sensitivity-', dir=parent)).resolve()
    source = SOURCE
    summary = {'scope': 'named perturbations only; not a code-wide mutation score',
               'contract': 'unordered within roots; retain partial output on late ordinary EIO',
               'cutils': str(cutils), 'cases': args.cases, 'ignore_cases': 16,
               'c_tests': C_TESTS, 'copied_files': FILES,
               'expected_controls': EXPECTED_MUTATIONS, 'runs': []}
    metadata = {name: {'source_file': filename, 'old': old, 'new': new,
                       'required_occurrences': 1, 'expected_rejection': expected}
                for name, filename, old, new, expected in MUTATIONS}
    summary['controls'] = metadata
    summary_path = root / 'summary.json'

    def record(name, status, results, expected=None, source_file=None):
        compiles = [result for result in results
                    if Path(result.get('stdout', '')).name.startswith('compile-')]
        compiled_valid = (len(compiles) == len(C_TESTS) + 1 and
                          all(result['returncode'] == 0 and not result['timeout']
                              and 'error' not in result for result in compiles))
        summary['runs'].append({'name': name, 'status': status,
                                'source_file': source_file,
                                'expected_rejection': expected, 'results': results,
                                'compiled_valid': compiled_valid})
        controls = [row for row in summary['runs'] if row['name'] in metadata]
        summary['compiled_valid_controls'] = sum(row['compiled_valid'] for row in controls)
        summary['behavioral_rejections'] = sum(row['status'] == 'rejected' for row in controls)
        summary_path.write_text(json.dumps(summary, indent=2) + '\n')
        print(name + ': ' + status, flush=True)

    def copy_tree(name):
        tree = root / name
        for relative in FILES:
            target = tree / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, target)
        (tree / 'build').mkdir()
        (tree / 'tmp').mkdir()
        return tree

    print('Artifacts: ' + str(root), flush=True)
    try:
        # Check every planned name before any gate: C fixtures must use our
        # owned TMPDIR, never their standalone long-path fallback to /tmp.
        names = ('baseline',) + tuple(mutation[0] for mutation in MUTATIONS)
        longest_tmpdir = max(len(os.fsencode(root / name / 'tmp'))
                             for name in names)
        if longest_tmpdir > 384:
            raise ValueError('planned TMPDIR exceeds 384-byte fixture budget: '
                             '%d bytes' % longest_tmpdir)
        # Validate every exact substitution against the unmodified source
        # before spending a baseline run. Missing anchors are never kills.
        for name, source_file, old, new, expected in MUTATIONS:
            if (source_file not in MUTATION_FILES or old == new or
                    (source / source_file).read_text().count(old) != 1):
                record(name, 'replacement-missing-or-ambiguous', [], expected, source_file)
                return 1
        baseline = copy_tree('baseline')
        status, results = gate(baseline, cutils, args.cases)
        record('baseline', 'passed' if status == 'passed' else 'baseline-' + status, results)
        if status != 'passed':
            return 1
        ok = True
        for name, source_file, old, new, expected in MUTATIONS:
            if source_file not in MUTATION_FILES:
                record(name, 'invalid-mutation-target', [], expected, source_file)
                ok = False
                continue
            tree = copy_tree(name)
            path = tree / source_file
            text = path.read_text()
            if text.count(old) != 1:
                record(name, 'replacement-missing-or-ambiguous', [], expected, source_file)
                ok = False
                continue
            path.write_text(text.replace(old, new, 1))
            status, results = gate(tree, cutils, args.cases)
            if status == 'passed':
                status = 'survived'
            record(name, status, results, expected, source_file)
            ok = ok and status == 'rejected'
        return 0 if (ok and summary['compiled_valid_controls'] == EXPECTED_MUTATIONS
                     and summary['behavioral_rejections'] == EXPECTED_MUTATIONS) else 1
    except (OSError, ValueError) as error:
        record('runner', 'error', [{'error': str(error)}])
        return 1


if __name__ == '__main__':
    sys.exit(main())
