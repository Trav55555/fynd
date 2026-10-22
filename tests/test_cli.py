#!/usr/bin/env python3
"""Independent black-box acceptance checks. Run: python3 tests/test_cli.py BINARY."""
import argparse
import errno
import importlib.util
import os
from pathlib import Path
import random
import stat
import subprocess
import tempfile


# Explicit sibling loading also works when invoked with Python's -I flag.
_spec = importlib.util.spec_from_file_location(
    'cli_output_oracle', Path(__file__).with_name('output_oracle.py'))
_output = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_output)


def fold(value):
    return value.translate(bytes.maketrans(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ',
                                          b'abcdefghijklmnopqrstuvwxyz'))


def oracle(roots, pattern=b'', kinds=(), extensions=(), hidden=False,
           insensitive=False, depth=128):
    result = []
    pattern = fold(pattern) if insensitive else pattern
    extensions = {fold(e) if insensitive else e for e in extensions}

    def visit(path, level):
        mode = os.lstat(path).st_mode
        kind = ('d' if stat.S_ISDIR(mode) else 'f' if stat.S_ISREG(mode)
                else 'l' if stat.S_ISLNK(mode) else 'o')
        name = path.rsplit(b'/', 1)[-1] if path != b'/' else b'/'
        name = fold(name) if insensitive else name
        stem, dot, suffix = name.rpartition(b'.')
        extension = suffix if dot and stem and suffix else None
        if (pattern in name and (not kinds or kind in kinds)
                and (not extensions or extension in extensions)):
            result.append(path)
        if kind == 'd' and level < depth:
            with os.scandir(path) as entries:
                names = sorted(entry.name for entry in entries)
            for child in names:
                if hidden or not child.startswith(b'.'):
                    visit(path + (b'' if path.endswith(b'/') else b'/') + child,
                          level + 1)

    for root in roots:
        visit(root.rstrip(b'/') or b'/', 0)
    return result


class Suite:
    def __init__(self, binary):
        self.binary = binary
        self.count = 0
        self.env = os.environ.copy()
        self.skips = []

    def check(self, label, args, expected=b'', status=0, diagnostic=False,
              expected_stderr=None, groups=None, delimiter=b'\0'):
        proc = subprocess.run([self.binary] + args, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=10, env=self.env)
        self.count += 1
        output_ok = (proc.stdout == expected if groups is None else
                     _output.matches_output(proc.stdout, groups, delimiter))
        if (proc.returncode != status or not output_ok
                or (expected_stderr is None and bool(proc.stderr) != diagnostic)
                or (expected_stderr is not None and proc.stderr != expected_stderr)):
            raise AssertionError(
                '%s args=%r\nstatus %r != %r\nstdout %r != %r\nstderr %r'
                % (label, args, proc.returncode, status, proc.stdout,
                   expected, proc.stderr))

    def records(self, label, args, records, groups=None, delimiter=b'\0', **kwargs):
        # Canonical lists describe membership, not arrival order within a root.
        self.check(label, args, b''.join(p + delimiter for p in records),
                   groups=[records] if groups is None else groups,
                   delimiter=delimiter, **kwargs)


def touch(path):
    with open(path, 'wb'):
        pass


def fixed(s):
    os.mkdir(b'r')
    os.mkdir(b'r/a')
    touch(b'r/a/needle.TXT')
    touch(b'r/B.txt')
    touch(b'r/space name')
    touch(b'r/line\nname')
    touch(b'r/\xff')
    os.mkdir(b'r/.hidden')
    touch(b'r/.hidden/visible')
    touch(b'r/.hidden/.secret')
    os.symlink(b'a', b'r/link')
    os.symlink(b'absent', b'r/broken')
    os.mkfifo(b'r/pipe')
    all_r = [b'r', b'r/B.txt', b'r/a', b'r/a/needle.TXT', b'r/broken',
             b'r/line\nname', b'r/link', b'r/pipe', b'r/space name', b'r/\xff']
    s.records('fixed byte multiset', [b'-0', b'', b'r'], all_r)
    s.check('newline records', [b'needle', b'r'], b'r/a/needle.TXT\n')
    s.records('default operands', [b'-0'], [b'.'] + [b'./' + p for p in all_r])
    s.records('dot is literal not all files', [b'.', b'r'],
              [b'r/B.txt', b'r/a/needle.TXT'], delimiter=b'\n')
    s.records('existing directory operand is pattern', [b'r'],
              [b'./r', b'./r/broken'], delimiter=b'\n')
    s.check('no match succeeds', [b'never-match', b'r'])
    s.records('type union and no pruning', [b'-0', b'-t', b'f', b'--type', b'l',
               b'needle', b'r'], [b'r/a/needle.TXT'])
    s.records('FIFO is leaf', [b'-0', b'--type', b'o', b'', b'r'], [b'r/pipe'])
    s.records('links never followed', [b'-0', b'-t', b'l', b'', b'r'],
              [b'r/broken', b'r/link'])
    for root in (b'r/link/', b'r/broken///', b'r/B.txt///'):
        s.records('root trailing slash', [b'-0', b'', root], [root.rstrip(b'/')])
    s.records('lexical root', [b'-0', b'--max-depth', b'1', b'', b'./r/a/../a//'],
              [b'./r/a/../a', b'./r/a/../a/needle.TXT'])
    s.records('duplicates and root order', [b'-0', b'', b'r/link', b'r/B.txt', b'r/link'],
              [b'r/link', b'r/B.txt', b'r/link'],
              groups=[[b'r/link'], [b'r/B.txt'], [b'r/link']])
    s.records('hidden explicit root', [b'-0', b'', b'r/.hidden'],
              [b'r/.hidden', b'r/.hidden/visible'])
    s.records('hidden children included', [b'-0', b'-H', b'--hidden', b'', b'r/.hidden'],
              [b'r/.hidden', b'r/.hidden/.secret', b'r/.hidden/visible'])
    s.records('slash root preserved', [b'-0', b'--max-depth', b'0', b'', b'///'], [b'/'])
    s.records('dot dot basename', [b'-0', b'--max-depth', b'0', b'..', b'..'], [b'..'])
    s.records('depth zero', [b'-0', b'--max-depth', b'0', b'', b'r'], [b'r'])
    s.records('depth one', [b'-0', b'--max-depth', b'1', b'', b'r'],
              [p for p in all_r if p.count(b'/') <= 1])
    s.records('last depth wins', [b'-0', b'--max-depth', b'0', b'--max-depth',
                                b'1', b'', b'r/a'], [b'r/a', b'r/a/needle.TXT'])
    s.records('missing roots continue', [b'-0', b'', b'missing', b'r/B.txt',
                                       b'also-missing', b'r/link'],
              [b'r/B.txt', b'r/link'], groups=[[], [b'r/B.txt'], [], [b'r/link']],
              status=1, diagnostic=True)
    s.records('diagnostic escapes bytes and continues',
              [b'-0', b'', b"missing-\x01\n\t\r\x1b'\\\x7f\xff", b'r/B.txt'],
              [b'r/B.txt'], groups=[[], [b'r/B.txt']], status=1,
              expected_stderr=(b"fynd: cannot stat root 'missing-"
                               b"\\x01\\x0a\\x09\\x0d\\x1b\\x27\\x5c\\x7f\\xff'\n"))
    os.mkdir(b'ext')
    for name in (b'.profile', b'.config.json', b'a.', b'a.txt', b'A.TXT', b'x.tar.gz',
                 b'caf\xc3\xa9.TXT', b'CAF\xc3\x89.txt'):
        touch(b'ext/' + name)
    os.mkdir(b'ext/dir.txt')
    os.symlink(b'absent', b'ext/link.txt')
    s.records('extension applies to all kinds', [b'-0', b'-e', b'txt', b'', b'ext'],
              [b'ext/CAF\xc3\x89.txt', b'ext/a.txt', b'ext/dir.txt', b'ext/link.txt'])
    s.records('extension union and dotfile', [b'-0', b'-H', b'-e', b'json',
              b'--extension', b'gz', b'', b'ext'], [b'ext/.config.json', b'ext/x.tar.gz'])
    s.records('dotfile and trailing dot lack suffix', [b'-0', b'-H', b'-e',
              b'profile', b'-e', b'a', b'', b'ext'], [])
    s.records('ASCII fold only', [b'-0', b'-i', b'--ignore-case', b'-e', b'txt',
              b'caf\xc3\xa9', b'ext'], [b'ext/caf\xc3\xa9.TXT'])
    s.records('fold extension', [b'-0', b'--ignore-case', b'-e', b'TXT', b'a.', b'ext'],
              [b'ext/A.TXT', b'ext/a.txt'])
    touch(b'-i')
    s.check('end options', [b'--', b'-i'], b'./-i\n')
    s.records('options stop at operand', [b'-0', b'', b'-i'], [b'-i'])
    for args in ([b'--wat'], [b'-it'], [b'--type=f'], [b'-t'], [b'-t', b'ff'],
                 [b'-t', b'x'], [b'-e'], [b'-e', b''], [b'-e', b'.txt'],
                 [b'-e', b'a/b'], [b'--max-depth'], [b'--max-depth', b'-1'],
                 [b'--max-depth', b'129'], [b'--max-depth', b'1x'],
                 [b'--max-depth', b''], [b'--max-depth', b'999999999999999999999'],
                 [b'', b''], [b'--help', b'r'], [b'--version', b'r'],
                 [b'-h', b'-0']):
        s.check('invalid invocation', list(args), status=2, diagnostic=True)
    for flag in (b'-h', b'--help', b'--version'):
        proc = subprocess.run([s.binary, flag], capture_output=True, timeout=10)
        s.count += 1
        assert proc.returncode == 0 and proc.stdout and not proc.stderr, flag
    os.mkdir(b'deep')
    path = b'deep'
    paths = [path]
    for _ in range(129):
        path += b'/d'
        os.mkdir(path)
        paths.append(path)
    s.records('explicit 128 cutoff', [b'-0', b'--max-depth', b'128', b'', b'deep'], paths[:129])
    s.records('default safety error', [b'-0', b'', b'deep'], paths[:129],
              status=1, diagnostic=True)
    os.mkdir(b'ignored')
    with open(b'ignored/.gitignore', 'wb') as stream:
        stream.write(b'*\n')
    touch(b'ignored/kept')
    s.records('no-ignore bypass', [b'--no-ignore', b'-0', b'', b'ignored'],
              [b'ignored', b'ignored/kept'])
    with open(b'ignored/.ignore', 'wb') as stream:
        stream.write(b'kept\n')
    s.records('default ignore rule', [b'-0', b'', b'ignored'], [b'ignored'])
    os.mkdir(b'locked')
    touch(b'locked/child')
    os.chmod(b'locked', 0)
    try:
        try:
            with os.scandir(b'locked') as entries:
                list(entries)
        except PermissionError:
            s.records('permission error continues', [b'-0', b'', b'locked', b'r/link'],
                      [b'locked', b'r/link'], groups=[[b'locked'], [b'r/link']],
                      status=1, diagnostic=True)
            s.records('cutoff avoids unreadable directory',
                      [b'-0', b'--max-depth', b'0', b'', b'locked'], [b'locked'])
        else:
            s.skips.append('permission denial unavailable for this user')
    finally:
        os.chmod(b'locked', 0o700)


def long_paths(s):
    # Fifteen directories, depth well below 128; no dependence on cwd length.
    with tempfile.TemporaryDirectory(prefix='long-', dir='.') as directory:
        root = os.fsencode(directory)
        paths = [root]
        try:
            for index in range(15):
                path = paths[-1] + b'/' + (b'd%02d-' % index) + b'x' * 92
                os.mkdir(path)
                paths.append(path)
            leaves = [paths[-1] + b'/a', paths[-1] + b'/z']
            for leaf in leaves:
                touch(leaf)
        except OSError as error:
            if error.errno not in (errno.ENAMETOOLONG, errno.ENOSYS,
                                   errno.EOPNOTSUPP):
                raise
            s.skips.append('long-path fixture unsupported: %s' % error)
        else:
            assert len(paths[-1]) > 1024
            s.records('long joined paths exact multiset', [b'-0', b'', root],
                      paths + leaves)
            s.records('long explicit subtree', [b'-0', b'', paths[-2]],
                      paths[-2:] + leaves)
            s.records('long explicit leaf roots preserve order',
                      [b'-0', b'', leaves[1], leaves[0]], list(reversed(leaves)),
                      groups=[[leaves[1]], [leaves[0]]])
            s.records('long root depth selection',
                      [b'-0', b'--max-depth', b'0', b'', paths[-1]], [paths[-1]])
            s.records('long subtree file selection',
                      [b'-0', b'-t', b'f', b'', root], leaves)

    # Explicit impossible root: exercise the host path error, not a finder limit.
    # No directories need creating, and the later leaf proves continuation.
    try:
        limit = os.pathconf(b'.', 'PC_PATH_MAX')
    except (OSError, ValueError) as error:
        s.skips.append('over-PATH_MAX root: pathconf unavailable: %s' % error)
        return
    if not 0 < limit <= 65536:
        s.skips.append('over-PATH_MAX root: no bounded positive PATH_MAX (%r)' % limit)
        return
    # Keep every component short so this probes path length, not NAME_MAX.
    impossible = b'/'.join([b'absent'] * (limit // 7 + 2))
    try:
        os.lstat(impossible)
    except OSError as error:
        if error.errno != errno.ENAMETOOLONG:
            s.skips.append('over-PATH_MAX root: host returned errno %r, not ENAMETOOLONG'
                           % error.errno)
            return
    else:
        s.skips.append('over-PATH_MAX root: host accepts path beyond reported limit')
        return
    s.records('over-PATH_MAX root error is not truncated and continues',
              [b'-0', b'', impossible, b'r/B.txt'], [b'r/B.txt'],
              groups=[[], [b'r/B.txt']], status=1,
              expected_stderr=b"fynd: cannot stat root '" + impossible + b"'\n")


def generated(s, cases, seed):
    rng = random.Random(seed)
    for case in range(cases):
        with tempfile.TemporaryDirectory(prefix='random-', dir='.') as directory:
            root = os.fsencode(directory)
            directories = [(root, 0)]
            for index in range(rng.randint(8, 35)):
                parent, level = rng.choice(directories)
                name = rng.choice([b'alpha', b'Beta', b'.hide', b'\xfe', b'line\n', b'x y'])
                name += str(index).encode() + rng.choice([b'', b'.txt', b'.TXT', b'.json', b'.'])
                path = parent + b'/' + name
                kind = rng.randrange(5)
                if kind == 0 and level < 4:
                    os.mkdir(path)
                    directories.append((path, level + 1))
                elif kind == 1:
                    os.symlink(b'nonexistent', path)
                elif kind == 2:
                    os.mkfifo(path)
                else:
                    touch(path)
            hidden = rng.choice([False, True])
            insensitive = rng.choice([False, True])
            depth = rng.choice([0, 1, 2, 4, 128])
            kinds = rng.choice([(), ('f',), ('d',), ('l', 'o'), ('f', 'd')])
            extensions = rng.choice([(), (b'txt',), (b'TXT', b'json')])
            pattern = rng.choice([b'', b'a', b'Beta', b'.', b'\xfe', b'not-found'])
            roots = [root]
            if case % 3 == 0:
                roots += [rng.choice(directories)[0], root]
            args = [b'-0', b'--max-depth', str(depth).encode()]
            if hidden:
                args.append(b'-H')
            if insensitive:
                args.append(b'-i')
            for kind in kinds:
                args += [b'-t', kind.encode()]
            for extension in extensions:
                args += [b'-e', extension]
            groups = [oracle([root], pattern, kinds, extensions, hidden, insensitive, depth)
                      for root in roots]
            expected = [path for group in groups for path in group]
            s.records('random seed=%d case=%d' % (seed, case),
                      args + [pattern] + roots, expected, groups=groups)
            for jobs in (b'-j2', b'-j4', b'-j8'):
                s.records('random threaded seed=%d case=%d' % (seed, case),
                          [jobs] + args + [pattern] + roots, expected, groups=groups)


def thread_matrix(s):
    # Many eligible sibling directories in one root; this is parity, not proof
    # of overlap (the private collector barrier supplies that proof).
    os.mkdir(b'wide')
    for index in range(40):
        directory = b'wide/d%02d' % index
        os.mkdir(directory)
        touch(directory + b'/line\nname')
        touch(directory + b'/.hidden')
        os.symlink(b'absent', directory + b'/link')
    for roots in ([b'wide'], [b'wide', b'r', b'wide']):
        for flags in ([], [b'-H'], [b'--max-depth', b'1'], [b'-t', b'l']):
            groups = [oracle([root], hidden=b'-H' in flags,
                             depth=1 if b'1' in flags else 128,
                             kinds=('l',) if b'l' in flags else ()) for root in roots]
            expected = [path for group in groups for path in group]
            for jobs in ([b'-j', b'1'], [b'-j2'], [b'--threads', b'4'], [b'-j8']):
                s.records('fixed thread parity', jobs + [b'-0'] + flags + [b''] + roots,
                          expected, groups=groups)
    for value in (b'', b'0', b'33', b'-1', b'+2', b'2x', b'99999999999999999999'):
        s.check('invalid jobs', [b'--threads', value], status=2, diagnostic=True)
    for args in ([b'-j'], [b'--threads'], [b'--threads=2'], [b'-j2', b'--help']):
        s.check('invalid jobs syntax', args, status=2, diagnostic=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', help='path to the fynd executable')
    parser.add_argument('--cases', type=int, default=100)
    parser.add_argument('--seed', type=int, default=1)
    options = parser.parse_args()
    if options.cases < 0:
        parser.error('--cases must be nonnegative')
    suite = Suite(os.fsencode(os.path.abspath(options.binary)))
    original = os.getcwd()
    with tempfile.TemporaryDirectory(prefix='fynd-cli-') as directory:
        try:
            os.chdir(directory)
            suite.env.update(HOME=os.path.abspath('home'),
                             XDG_CONFIG_HOME=os.path.abspath('home'),
                             GIT_CONFIG_GLOBAL=os.devnull,
                             GIT_CONFIG_NOSYSTEM='1')
            fixed(suite)
            long_paths(suite)
            thread_matrix(suite)
            generated(suite, options.cases, options.seed)
        finally:
            os.chdir(original)
    print('PASS %d subprocess checks (seed %d, %d generated cases)' %
          (suite.count, options.seed, options.cases))
    for skip in suite.skips:
        print('SKIP ' + skip)


if __name__ == '__main__':
    main()
