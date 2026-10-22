"""Randomized ignore differential: fynd's file selection against Git itself.

Each case builds a random tree in a fresh repository with random .gitignore
rules (root and nested) and .git/info/exclude, then compares the regular files
fynd selects with `git ls-files --others --exclude-standard`, serially and
with the default thread count. Hidden names are outside this comparison
(fynd needs -H for them; Git has no such rule). Global Git configuration is
isolated so only repository rules apply.

usage: test_ignore_git.py FYND [--cases N] [--seed S]
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile

NAMES = ['a', 'b', 'ab', 'ba', 'foo', 'a.c', 'b.c', 'x y', '#a', '!a', 'a ', 'a\\',
         '[a]', 'a*', 'zzz']
COMPS = ['a', 'b', 'ab', '*', '**', '**', '?', '[ab]', '[!a]', 'a*', '*b', '*.c',
         'foo', 'zzz', '\\#a', '\\!a', 'x y', 'a\\ ', 'a\\\\', '\\[a]', '[a-b]', 'a?', '\\a']
ENV = dict(os.environ, HOME='/nonexistent', XDG_CONFIG_HOME='/nonexistent',
           GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL='/dev/null', LC_ALL='C')


def rule(rng):
    r = '/'.join(rng.choice(COMPS) for _ in range(rng.randint(1, 4)))
    if rng.random() < 0.2:
        r = '/' + r
    if rng.random() < 0.2:
        r += '/'
    if rng.random() < 0.2:
        r = '!' + r
    if rng.random() < 0.1:
        r += ' ' * rng.randint(1, 2)  # unescaped trailing spaces are trimmed
    return r


def tree(rng, base, depth):
    for name in rng.sample(NAMES, rng.randint(1, 4)):
        path = os.path.join(base, name)
        if depth < 3 and rng.random() < 0.45:
            os.mkdir(path)
            tree(rng, path, depth + 1)
        else:
            open(path, 'w').close()


def rules_file(rng, path, count):
    with open(path, 'w') as f:
        f.write('\n'.join(rule(rng) for _ in range(count)) + '\n')


def selected(fynd, root, jobs):
    argv = [fynd, '-0', '-t', 'f'] + jobs + ['', '.']
    out = subprocess.run(argv, cwd=root, env=ENV, capture_output=True)
    paths = {p[2:] for p in out.stdout.decode('utf-8', 'surrogateescape').split('\0') if p}
    return out.returncode, out.stderr, paths


def case(fynd, seed, scratch):
    rng = random.Random(seed)
    root = tempfile.mkdtemp(dir=scratch)
    try:
        subprocess.run(['git', 'init', '-q', root], check=True, env=ENV)
        tree(rng, root, 0)
        rules_file(rng, os.path.join(root, '.gitignore'), rng.randint(1, 5))
        dirs = [d for d, _, _ in os.walk(root) if '.git' not in d.split(os.sep) and d != root]
        if dirs and rng.random() < 0.4:
            rules_file(rng, os.path.join(rng.choice(dirs), '.gitignore'), rng.randint(1, 3))
        if rng.random() < 0.3:
            rules_file(rng, os.path.join(root, '.git', 'info', 'exclude'), rng.randint(1, 2))
        git = subprocess.run(['git', 'ls-files', '-z', '--others', '--exclude-standard'],
                             cwd=root, env=ENV, capture_output=True, check=True).stdout
        want = {p for p in git.decode('utf-8', 'surrogateescape').split('\0')
                if p and not any(c.startswith('.') for c in p.split('/'))}
        for jobs in (['-j1'], []):
            rc, err, got = selected(fynd, root, jobs)
            if rc != 0 or err or got != want:
                return dict(seed=seed, jobs=jobs or 'default', rc=rc, err=err[:200],
                            only_fynd=sorted(got - want), only_git=sorted(want - got))
        return None
    finally:
        shutil.rmtree(root)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('fynd')
    parser.add_argument('--cases', type=int, default=300)
    parser.add_argument('--seed', type=int, default=0)
    args = parser.parse_args(argv)
    git = shutil.which('git')
    if git is None:
        print('SKIP Git absent: randomized Git ignore differential not run')
        return 0
    fynd = os.path.abspath(args.fynd)
    with tempfile.TemporaryDirectory(prefix='fynd-ignore-git-') as scratch:
        failures = [f for f in (case(fynd, args.seed + i, scratch) for i in range(args.cases)) if f]
    for failure in failures[:5]:
        print('MISMATCH', failure)
    if failures:
        print(f'FAIL {len(failures)} of {args.cases} Git ignore cases')
        return 1
    print(f'PASS {args.cases} Git ignore cases (seed {args.seed}): serial and default threads')
    return 0


if __name__ == '__main__':
    sys.exit(main())
