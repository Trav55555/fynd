#!/usr/bin/env python3
"""Independent ignore acceptance: literal fixtures plus fd/Git subset oracles."""
import argparse
from collections import Counter
import importlib.util
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import tempfile


# Explicit path import preserves standalone execution under python -I.
_spec = importlib.util.spec_from_file_location(
    'ignore_output_oracle', Path(__file__).with_name('output_oracle.py'))
_output = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_output)


def run(argv, env, cwd):
    p = subprocess.Popen(argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, start_new_session=True)
    try:
        out, err = p.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.communicate()
        raise AssertionError('10 second timeout: %r' % (argv,))
    return p.returncode, out, err


class Suite:
    def __init__(self, binary, root):
        self.binary, self.root = binary, root
        self.env = os.environ.copy()
        home = os.path.join(root, 'home')
        os.mkdir(home)
        self.env.update(HOME=home, XDG_CONFIG_HOME=home,
                        GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1')
        self.count = 0
        self.skips = []

    def put(self, path, data=b''):
        full = os.path.join(self.root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as f:
            f.write(data)

    def check(self, label, paths, roots=('tree',), flags=(), status=0, error=False,
              groups=None, cwd=None):
        expected = b''.join(os.fsencode(p) + b'\0' for p in paths)
        if groups is None:
            if len(roots) != 1:
                raise ValueError('multiple roots require independent expected groups')
            groups = [paths]
        groups = [[os.fsencode(p) for p in group] for group in groups]
        for jobs in ('-j1', '-j2', '-j4', '-j8', '--sort'):
            rc, out, err = run([self.binary, jobs, '-0', '-t', 'f', *flags,
                                '', *roots], self.env,
                               os.path.join(self.root, cwd) if cwd else self.root)
            self.count += 1
            assert (rc == status and bool(err) == error and
                    _output.matches_output(out, groups)), (
                label, jobs, rc, out, err, expected)


def literals(s):
    s.put('tree/.ignore', b'*.tmp\n!keep.tmp\npruned/\n[broken\n')
    for p in ('a.tmp', 'keep.tmp', 'visible', '.secret', '[broken',
              'pruned/keep', 'sub/a.tmp', 'sub/keep.tmp'):
        s.put('tree/' + p)
    s.put('tree/pruned/.ignore', b'!keep\n')
    s.put('tree/sub/.ignore', b'!a.tmp\nkeep.tmp\n!keep.tmp\n')
    s.check('deepest last-line and pruning',
            ['tree/[broken', 'tree/keep.tmp', 'tree/sub/a.tmp',
             'tree/sub/keep.tmp', 'tree/visible'])
    s.check('explicit ignored root', ['tree/pruned/keep'], ('tree/pruned',))
    s.check('explicit hidden leaf', ['tree/.secret'], ('tree/.secret',))
    s.check('ancestor outside explicit root', ['tree/sub/a.tmp', 'tree/sub/keep.tmp'],
            ('tree/sub',))
    s.check('lexical root', ['./tree/sub/../sub/a.tmp', './tree/sub/../sub/keep.tmp'],
            ('./tree/sub/../sub',))
    absolute = os.path.join(s.root, 'tree/sub')
    s.check('absolute root', [absolute + '/a.tmp', absolute + '/keep.tmp'], (absolute,))
    s.put('physical/target/parent/.ignore', b'blocked\n')
    s.put('physical/target/parent/blocked')
    s.put('physical/target/parent/kept')
    os.makedirs(os.path.join(s.root, 'physical/target/parent/leaf'))
    os.makedirs(os.path.join(s.root, 'physical/view'))
    os.symlink('../target/parent/leaf', os.path.join(s.root, 'physical/view/link'))
    s.check('physical symlink parent rules', ['physical/view/link/../kept'],
            ('physical/view/link/..',))
    s.check('bypass all', ['tree/[broken', 'tree/a.tmp', 'tree/keep.tmp',
                         'tree/pruned/keep', 'tree/sub/a.tmp',
                         'tree/sub/keep.tmp', 'tree/visible'], flags=('--no-ignore',))
    s.put('missing/ok')
    s.check('missing sources quiet', ['missing/ok'], ('missing',))
    s.put('bad/.ignore', b'x\0y')
    s.put('bad/child')
    s.check('NUL source error continues', ['missing/ok'], ('bad', 'missing'),
            status=1, error=True, groups=[[], ['missing/ok']])
    for name in ('fifo', 'device'):
        os.mkdir(os.path.join(s.root, name))
        path = os.path.join(s.root, name, '.ignore')
        if name == 'fifo':
            os.mkfifo(path)
        else:
            os.symlink('/dev/null', path)
        s.check('nonregular source does not block', ['missing/ok'],
                (name, 'missing'), status=1, error=True, groups=[[], ['missing/ok']])
    s.put('rules', b'child\n')
    s.put('linked/child')
    os.symlink('../rules', os.path.join(s.root, 'linked/.ignore'))
    s.check('regular symlink source', [], ('linked',))
    s.put('denied/.ignore', b'*\n')
    s.put('denied/child')
    path = os.path.join(s.root, 'denied/.ignore')
    os.chmod(path, 0)
    try:
        try:
            with open(path, 'rb'):
                pass
        except PermissionError:
            s.check('source EACCES continues', ['missing/ok'],
                    ('denied', 'missing'), status=1, error=True,
                    groups=[[], ['missing/ok']])
        else:
            s.skips.append('source EACCES unavailable for this user')
    finally:
        os.chmod(path, 0o600)


def precedence(s):
    # Marker file deliberately points nowhere: no worktree gitdir resolution.
    s.put('repo/.git', b'gitdir: /nonexistent\n')
    s.put('home/git/ignore', b'*.dat\n')
    s.put('repo/.gitignore', b'!keep.dat\nblocked\n')
    s.put('repo/.ignore', b'!blocked\nkeep.dat\n')
    for p in ('keep.dat', 'drop.dat', 'blocked', 'ok'):
        s.put('repo/' + p)
    s.put('repo/nested/.git', b'gitdir: /also-nonexistent\n')
    s.put('repo/nested/.gitignore', b'!local.dat\n')
    s.put('repo/nested/local.dat')
    s.check('class precedence and nested marker boundary',
            ['repo/blocked', 'repo/nested/local.dat', 'repo/ok'], ('repo',))
    s.put('repo/nested/.ignore', b'!keep.dat\n')
    s.put('repo/nested/keep.dat')
    s.check('deep .ignore overrides ancestor .ignore',
            ['repo/nested/keep.dat', 'repo/nested/local.dat'], ('repo/nested',))
    s.put('exclude/.git/info/exclude', b'*.dat\n')
    s.put('exclude/.gitignore', b'!keep.dat\n')
    s.put('exclude/keep.dat')
    s.put('exclude/drop.dat')
    s.check('Git above exclude above global', ['exclude/keep.dat'], ('exclude',))
    s.put('home/.config/git/ignore', b'*.dat\n')
    xdg = s.env.pop('XDG_CONFIG_HOME')
    try:
        s.check('HOME global fallback repository-relative', ['repo/blocked',
                'repo/nested/keep.dat', 'repo/nested/local.dat', 'repo/ok'], ('repo',))
    finally:
        s.env['XDG_CONFIG_HOME'] = xdg
    s.put('repo/.ignore', b'!blocked\nkeep.dat\n!.secret\n!.conf/\n')
    for p in ('.secret', '.other', '.conf/inner', '.conf/.deeper'):
        s.put('repo/' + p)
    s.check('negation re-includes hidden', ['repo/.conf/inner', 'repo/.secret',
             'repo/blocked', 'repo/nested/keep.dat', 'repo/nested/local.dat', 'repo/ok'],
            ('repo',))
    unhidden = sorted(os.path.relpath(os.path.join(d, f), s.root)
                      for d, _, files in os.walk(os.path.join(s.root, 'repo')) for f in files
                      if not any(c.startswith('.') for c in
                                 os.path.relpath(os.path.join(d, f), s.root).split('/')))
    s.check('--no-ignore re-includes no hidden name', unhidden, ('repo',), ('--no-ignore',))


def fd_binary(s):
    fd = shutil.which('fd') or shutil.which('fdfind')
    if fd:
        rc, version, _ = run([fd, '--version'], s.env, s.root)
        if rc or b'10.3.0' not in version:
            fd = None
    return fd


def fd_sources(s):
    """fd's own sources: .fdignore above .ignore, then fd's global file,
    whose rules are relative to the working directory."""
    fd = fd_binary(s)
    if not fd:
        s.skips.append('fd 10.3.0 unavailable: fd ignore-source oracle')

    def check(label, paths, roots, cwd=None, flags=(), oracle=True):
        s.check(label, paths, roots, flags, cwd=cwd)
        if fd and oracle:
            # fd builds disagree on anchored global rules under absolute or
            # '..' roots (Debian's anchors at the working directory, as fynd
            # does; upstream's never matches), so those cases are literal.
            rc, out, err = run([fd, '--color', 'never', '-j1', '-0', '-tf', *flags,
                                '', *roots], s.env,
                               os.path.join(s.root, cwd) if cwd else s.root)
            records = out[:-1].split(b'\0') if out else []
            assert rc == 0 and not err and Counter(records) == Counter(
                os.fsencode(p) for p in paths), (label, records, paths)

    s.put('fdt/.ignore', b'*.log\nhidden-by-ignore\n')
    s.put('fdt/.fdignore', b'!hidden-by-ignore\nonly-fd\n')
    s.put('fdt/sub/.fdignore', b'!keep.log\n')
    for p in ('a.log', 'hidden-by-ignore', 'only-fd', 'ok', 'sub/keep.log', 'sub/b.log'):
        s.put('fdt/' + p)
    check('.fdignore above .ignore, outside a repository',
          ['fdt/hidden-by-ignore', 'fdt/ok', 'fdt/sub/keep.log'], ('fdt',))
    check('ancestor .fdignore', ['fdt/sub/keep.log'], ('fdt/sub',))
    s.put('home/fd/ignore', b'/fdg/top\nloose.tmp\nfdg/sub/anchored\n')
    for p in ('top', 'loose.tmp', 'sub/top', 'sub/loose.tmp', 'sub/anchored', 'sub/kept'):
        s.put('fdg/' + p)
    check('global fd rules anchor at the working directory',
          ['fdg/sub/kept', 'fdg/sub/top'], ('fdg',))
    absolute = os.path.join(s.root, 'fdg')
    check('global fd rules under an absolute root',
          [absolute + '/sub/kept', absolute + '/sub/top'], (absolute,), oracle=False)
    check('global fd anchors move with the working directory',
          ['./sub/anchored', './sub/kept', './sub/top', './top'],
          ('.',), cwd='fdg')
    check('outside the working directory only unanchored rules apply',
          ['../fdg/sub/anchored', '../fdg/sub/kept', '../fdg/sub/top', '../fdg/top'],
          ('../fdg',), cwd='fdt', oracle=False)
    s.put('fdg/kept')
    s.put('fdg/.ignore', b'!loose.tmp\n')
    check('.ignore negation above the global fd file',
          ['fdg/kept', 'fdg/loose.tmp', 'fdg/sub/kept', 'fdg/sub/loose.tmp', 'fdg/sub/top'],
          ('fdg',))
    check('--no-ignore bypasses fd sources',
          ['fdg/kept', 'fdg/loose.tmp', 'fdg/sub/anchored', 'fdg/sub/kept',
           'fdg/sub/loose.tmp', 'fdg/sub/top', 'fdg/top'], ('fdg',), flags=('--no-ignore',))
    xdg = s.env.pop('XDG_CONFIG_HOME')
    try:
        s.put('home/.config/fd/ignore', b'kept\n')
        check('HOME fallback for the global fd file',
              ['fdg/loose.tmp', 'fdg/sub/anchored', 'fdg/sub/loose.tmp', 'fdg/sub/top',
               'fdg/top'], ('fdg',))
    finally:
        s.env['XDG_CONFIG_HOME'] = xdg
        os.remove(os.path.join(s.root, 'home/fd/ignore'))


def external(s, cases, seed):
    git = shutil.which('git')
    fd = shutil.which('fd') or shutil.which('fdfind')
    if not git:
        s.skips.append('Git absent: independent check-ignore subset')
    if fd:
        rc, version, _ = run([fd, '--version'], s.env, s.root)
        if rc or b'10.3.0' not in version:
            s.skips.append('fd 10.3.0 unavailable: independent fd subset')
            fd = None
    else:
        s.skips.append('fd absent: independent fd subset')
    rng = random.Random(seed)
    for case in range(cases):
        root = 'seed%03d' % case
        # Supported subset only: no malformed classes, nested repos, or class
        # precedence differences. Files only; directories cannot be pruned here.
        names = ['a%02d.txt' % i for i in range(24)]
        blocked = set(rng.sample(names, rng.randrange(1, 20)))
        rules = ''.join(n + '\n' for n in sorted(blocked)).encode()
        s.put(root + '/.gitignore', rules)
        for n in names:
            s.put(root + '/' + n)
        if git:
            rc, _, err = run([git, 'init', '-q', root], s.env, s.root)
            assert rc == 0, err
            for n in names:
                rc, _, err = run([git, 'check-ignore', '--no-index', '--', n],
                                 s.env, os.path.join(s.root, root))
                assert rc in (0, 1) and not err
                assert (rc == 0) == (n in blocked)
        else:
            s.put(root + '/.git', b'gitdir: /nonexistent\n')
        expected = [root + '/' + n for n in names if n not in blocked]
        s.check('seed=%d case=%d' % (seed, case), expected, (root,))
        if fd:
            rc, out, err = run([fd, '--color', 'never', '--hidden', '--ignore-case',
                                '--fixed-strings', '-tf', '-0', '-j1', '', root],
                               s.env, s.root)
            assert rc == 0 and not err
            # Both tools are unordered; preserve multiplicity and reject broken framing.
            assert not out or out.endswith(b'\0'), out
            records = out[:-1].split(b'\0') if out else []
            records = [p for p in records if b'/.git/' not in p
                       and not p.endswith(b'/.gitignore')]
            assert Counter(records) == Counter(os.fsencode(p) for p in expected), (records, expected)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary')
    p.add_argument('--cases', type=int, default=16)
    p.add_argument('--seed', type=int, default=47)
    a = p.parse_args()
    if a.cases < 0:
        p.error('--cases must be nonnegative')
    with tempfile.TemporaryDirectory(prefix='fynd-ignore-') as root:
        s = Suite(os.path.abspath(a.binary), root)
        literals(s)
        precedence(s)
        fd_sources(s)
        external(s, a.cases, a.seed)
        print('PASS %d fynd checks' % s.count)
        for skip in s.skips:
            print('SKIP ' + skip)


if __name__ == '__main__':
    main()
