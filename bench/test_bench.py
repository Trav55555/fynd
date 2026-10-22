"""Tiny fake-tool tests; no installed search tools or fixture benchmark needed."""
import contextlib
import io
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from bench import bench


class BenchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def executable(self, name, body):
        path = self.root / name
        path.write_text('#!/usr/bin/env python3\n' + body)
        path.chmod(0o755)
        return str(path)

    def test_manifest_validation(self):
        path = self.root / 'out'
        for data in (b'a\0a\0', b'a', b'', b'b\0', b'a\0\0'):
            path.write_bytes(data)
            with self.assertRaises(RuntimeError):
                bench.validate(path, [b'a'])
        path.write_bytes(b'b\0a\0')
        bench.validate(path, [b'a', b'b'])
        path.write_bytes(b'b\0a\0a\0')
        bench.validate(path, [b'a', b'a', b'b'])
        with self.assertRaises(RuntimeError):
            bench.validate(path, [b'a', b'b', b'b'])

        # Exercise the CLI oracle via an explicit path, including python -I use.
        spec = importlib.util.spec_from_file_location(
            'test_output_oracle', Path(__file__).resolve().parents[1] / 'tests/output_oracle.py')
        output = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(output)
        paths = [b'r/a', b'r/a\nr/b', b'r/b\nr/c', b'r/\xff\r\nx', b'r/a']
        groups = [paths, [b'next\nroot']]
        # The first token has a shorter valid prefix: greedy LF parsing fails.
        first = b''.join(p+b'\n' for p in [paths[1], paths[0], *paths[2:]])
        raw = first + b'next\nroot\n'
        self.assertTrue(output.matches_output(raw, groups, b'\n'))
        for bad in (raw[:-1], raw+b'r/a\n', raw.replace(b'\xff', b'\xfe'),
                    raw.replace(b'\r\n', b'\n'), b'next\nroot\n'+first,
                    raw.replace(b'r/a\n', b'', 1)):
            self.assertFalse(output.matches_output(bad, groups, b'\n'), bad)
        nul = b''.join(p+b'\0' for p in reversed(paths)) + b'next\nroot\0'
        self.assertTrue(output.matches_output(nul, groups))
        for bad in (nul[:-1], nul+b'\0', nul.replace(b'r/a\0', b'r/unknown\0', 1),
                    b'next\nroot\0'+nul[:-len(b'next\nroot\0')]):
            self.assertFalse(output.matches_output(bad, groups), bad)

    def test_execute_rejects_status_stderr_and_timeout(self):
        for name, body in [('exit', 'raise SystemExit(3)\n'),
                           ('stderr', 'import sys\nsys.stderr.write("bad")\n'),
                           ('timeout', 'import time\ntime.sleep(10)\n')]:
            tool = self.executable(name, body)
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                bench.execute([tool], self.root, self.root / 'out', self.root / 'err', .1)

    def test_rejected_before_any_measurement(self):
        for mode in ('wrong', 'missing', 'duplicate', 'truncated', 'exit', 'timeout', 'stderr'):
            body = ('import sys, time\n'
                    'if "--version" in sys.argv:\n print("GNU fake 1"); sys.exit()\n')
            body += {'wrong': 'sys.stdout.buffer.write(b"wrong\\0")\n',
                     'missing': 'pass\n',
                     'duplicate': 'sys.stdout.buffer.write(b"wide/file\\0wide/file\\0")\n',
                     'truncated': 'sys.stdout.buffer.write(b"wide/file")\n',
                     'exit': 'sys.exit(2)\n', 'timeout': 'time.sleep(10)\n',
                     'stderr': 'sys.stderr.write("bad")\n'}[mode]
            tool = self.executable(mode, body)
            parent = self.root / ('results-' + mode)
            # One tiny fixture, retaining the same all-tools barrier as production.
            def tiny(base, quick):
                (base / 'wide').mkdir()
                (base / 'wide' / 'file').touch()
                return {}, {'wide': [b'wide/file']}
            with patch.object(bench, 'fixture', tiny), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                status = bench.main(['--fynd', tool, '--find', tool, '--time', tool,
                                     '--fd', str(self.root / 'absent'), '--output', str(parent),
                                     '--timeout', '1', '--repeats', '1'])
            self.assertEqual(status, 1)
            run = next(parent.iterdir())
            self.assertEqual(json.loads((run / 'failure.json').read_text())['status'], 'FAILED')
            self.assertFalse((run / 'report.json').exists())
            self.assertFalse(list(run.glob('*.rss')))
            metadata = json.loads((run / 'metadata.json').read_text())
            self.assertEqual(metadata['tools']['fd']['status'], 'SKIP')

    def test_fixture_deterministic_and_hidden(self):
        first, second = self.root / 'a', self.root / 'b'
        first.mkdir()
        second.mkdir()
        p1, m1 = bench.fixture(first, True)
        p2, m2 = bench.fixture(second, True)
        self.assertEqual((p1, m1), (p2, m2))
        self.assertTrue(any(b'/.' in p for paths in m1.values() for p in paths))
        for paths in m1.values():
            self.assertEqual(len(paths), len(set(paths)))

    def test_success_with_fake_tools(self):
        search = self.executable('search',
            'import os, sys\n'
            'if "--version" in sys.argv:\n print("GNU fake 1"); sys.exit()\n'
            'root = "wide"\n'
            'pattern = "needle" if any("needle" in a for a in sys.argv) else ""\n'
            'for directory, dirs, files in os.walk(root):\n'
            ' for name in files:\n'
            '  if pattern in name: sys.stdout.buffer.write(os.fsencode(directory + "/" + name) + b"\\0")\n')
        timer = self.executable('timer',
            'import pathlib, subprocess, sys\n'
            'if "--version" in sys.argv:\n print("GNU time fake"); sys.exit()\n'
            'pathlib.Path(sys.argv[4]).write_text("123\\n")\n'
            'sys.exit(subprocess.call(sys.argv[6:]))\n')
        def tiny(base, quick):
            (base / 'wide').mkdir()
            (base / 'wide' / 'needle').touch()
            return {}, {'wide': [b'wide/needle']}
        parent = self.root / 'success'
        with patch.object(bench, 'fixture', tiny), contextlib.redirect_stdout(io.StringIO()):
            status = bench.main(['--fynd', search, '--find', search, '--fd', search,
                                 '--time', timer, '--output', str(parent), '--repeats', '2'])
        self.assertEqual(status, 0)
        report = json.loads((next(parent.iterdir()) / 'report.json').read_text())
        self.assertEqual(report['status'], 'SUCCESS')
        self.assertEqual(len(report['samples']), 12)
        self.assertEqual(len(report['validation']), 6)
        self.assertNotEqual(report['order'][0], report['order'][1])
        self.assertTrue(all(s['peak_rss_kib'] == 123 for s in report['samples']))

    def test_positive(self):
        for value in ('0', '-1'):
            with self.assertRaises(Exception):
                bench.positive(value)
        self.assertEqual(bench.positive('5'), 5)


if __name__ == '__main__':
    unittest.main()
