"""Failure-sensitive tests of the benchmark, not performance measurements."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from bench import comprehensive as c


class ComprehensiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.dataset = c.fixtures.Dataset('r', self.base, 'raw')
        for name in [b'a/z.c', b'a-', b'ASCII.C', b'.hidden/child', b'line\nname']:
            self.dataset.file(name)
        self.dataset.link(b'link', b'a')
        self.scratch = self.base/'scratch'; self.scratch.mkdir()

    def executable(self, name, body):
        path=self.base/name
        path.write_text('#!/usr/bin/env python3\n'+body)
        path.chmod(0o755)
        return str(path)

    def test_independent_dfs_not_global_path_sort(self):
        expected=c.oracle(c.Case(self.dataset,'all'))
        self.assertEqual(expected.paths,[b'r/.hidden/child',b'r/ASCII.C',b'r/a/z.c',b'r/a-',b'r/line\nname'])
        self.assertNotEqual(expected.paths,sorted(expected.paths))
        self.assertEqual(c.oracle(c.Case(self.dataset,'visible',hidden=False)).paths,
                         [b'r/ASCII.C',b'r/a/z.c',b'r/a-',b'r/line\nname'])
        self.assertEqual(c.oracle(c.Case(self.dataset,'depth',depth=1)).paths,
                         [b'r/ASCII.C',b'r/a-',b'r/line\nname'])

    def test_filter_union_fold_extensions_links_and_duplicate_roots(self):
        self.assertEqual(c.oracle(c.Case(self.dataset,'fold',pattern=b'ascii',fold=True,extensions=(b'c',))).paths,[b'r/ASCII.C'])
        self.assertEqual(c.oracle(c.Case(self.dataset,'links',kinds='l')).paths,[b'r/link'])
        one=c.oracle(c.Case(self.dataset,'one')).paths
        self.assertEqual(c.oracle(c.Case(self.dataset,'dups',roots=(b'',b''))).paths,one+one)
        self.assertEqual(c.oracle(c.Case(self.dataset,'root',kinds='d',depth=0)).paths,[b'r'])

    def test_validator_rejects_wrong_truncated_duplicate_and_root_mixing(self):
        oracle=c.Expected([b'r/a',b'r/b'])
        for bad in [b'',b'r/a\0',b'r/a\0r/a\0',b'r/a\0r/b',b'r/a\0r/c\0']:
            with self.subTest(bad=bad),self.assertRaises(RuntimeError):oracle.validate(bad,False)
        oracle.validate(b'r/b\0r/a\0',False)
        oracle.validate(b'r/a\0r/b\0')
        c.Expected([]).validate(b'')
        repeated=c.Expected([b'r/a',b'r/a',b'r/b'])
        repeated.validate(b'r/b\0r/a\0r/a\0')
        with self.assertRaises(RuntimeError):repeated.validate(b'r/b\0r/b\0r/a\0')
        grouped=c.Expected([b'r/a',b'r/b',b's/line\n\xff',b'r/a',b'r/b'],[2,1,2])
        grouped.validate(b'r/b\0r/a\0s/line\n\xff\0r/a\0r/b\0',grouped=True)
        mixed=b'r/a\0s/line\n\xff\0r/b\0r/a\0r/b\0'
        grouped.validate(mixed)  # External tools only promise a whole-output multiset.
        with self.assertRaises(RuntimeError):grouped.validate(mixed,grouped=True)

    def test_file_and_pipe_reject_status_stderr_timeout(self):
        for sink in ['file','pipe']:
            for name,body in [('exit','raise SystemExit(3)\n'),
                              ('err','import sys;sys.stderr.write("bad")\n'),
                              ('timeout','import time;time.sleep(10)\n')]:
                tool=self.executable(sink+name,body)
                with self.subTest(sink=sink,name=name),self.assertRaises(RuntimeError):
                    c.invoke([tool],c.Case(self.dataset,'test',sink=sink),self.scratch,.1)

    def test_both_sinks_capture_and_validate_every_byte(self):
        tool=self.executable('good','import sys;sys.stdout.buffer.write(b"r/b\\0r/a\\0")\n')
        for sink in ['file','pipe']:
            elapsed,data=c.invoke([tool],c.Case(self.dataset,'good',sink=sink),self.scratch,5)
            self.assertGreater(elapsed,0)
            self.assertEqual(data,b'r/b\0r/a\0')
            self.assertEqual(c.Expected([b'r/a',b'r/b']).validate(data,grouped=True)['output_records'],2)

    def test_measurement_wrapper_records_cpu_rss_and_output(self):
        search=self.executable('search','import sys;sys.stdout.buffer.write(b"r/a\\0")\n')
        timer=self.executable('timer','import sys,pathlib,subprocess\n'
             'pathlib.Path(sys.argv[sys.argv.index("-o")+1]).write_text("123 0.00 0.00 1 0\\n")\n'
             'sys.exit(subprocess.call(sys.argv[sys.argv.index("--")+1:]))\n')
        for sink in ['file','pipe']:
            data,result=c.measure([search],c.Case(self.dataset,'x',sink=sink),self.scratch,timer,5)
            self.assertEqual(data,b'r/a\0');self.assertEqual(result['peak_rss_kib'],123)
            self.assertEqual(result['cpu_ns'],result['user_cpu_ns']+result['system_cpu_ns'])
            self.assertAlmostEqual(result['mean_cores'],result['cpu_ns']/result['elapsed_ns'])

    def test_invalid_scope_cannot_silently_run(self):
        def fake_read(path,*args,**kwargs):
            if str(path)=='/proc/self/cgroup':return '0::/fake\n'
            if path.name=='memory.max':return 'max\n'
            if path.name=='memory.swap.max':return '0\n'
            raise AssertionError(path)
        with patch.object(Path,'read_text',fake_read),patch.object(os,'getpriority',return_value=15),patch.object(c.resource,'getrlimit',return_value=(0,0)):
            with self.assertRaisesRegex(RuntimeError,'requires MemoryMax'):c.limits()

    def test_fd_adapters_preserve_requested_semantics(self):
        case=c.Case(self.dataset,'ext',pattern=b'ASCII',extensions=(b'c',))
        argv=c.arguments(case,'fd','fd',1)
        self.assertNotIn('-e',argv)
        self.assertNotIn('--fixed-strings',argv)
        self.assertIn('--and',argv)
        self.assertTrue(argv[-2].startswith('(?s-u)^.+'))
        directory=c.arguments(c.Case(self.dataset,'dirs',kinds='d',pattern=b'dir'),'fd','fd',1)
        self.assertEqual(directory[directory.index('--format')+1],'{}')

    def test_bad_warmup_output_is_retained(self):
        with self.assertRaises(RuntimeError):
            c.validate_output(c.Expected([b'right']),b'wrong\0',False,self.scratch,grouped=True)
        self.assertEqual((self.scratch/'stdout.bin').read_bytes(),b'wrong\0')

    def test_initialization_failure_has_structured_artifact(self):
        parent=self.base/'init-error'
        with patch.object(c,'limits',return_value={}):
            with self.assertRaises(RuntimeError):
                c.main(['--fynd',str(self.base/'absent'),'--output',str(parent),'--quick'])
        run=next(parent.iterdir())
        self.assertEqual(json.loads((run/'failure.json').read_text())['status'],'FAILED')
        self.assertFalse((run/'report.json').exists())

    def test_orchestration_validates_before_timing_and_records_every_sample(self):
        # Reuse the five-file fixture with overlapping, ordered root groups.
        case=c.Case(self.dataset,'all',roots=(b'a',b'',b'.hidden'))
        expected=c.oracle(case)
        # Deliberately unsorted within each group at every runtime validation stage.
        chunks=[];offset=0
        for size in expected.group_sizes:
            chunks.append(b''.join(path+b'\0' for path in reversed(expected.paths[offset:offset+size])))
            offset+=size
        payload=b''.join(chunks)
        self.assertNotEqual(payload,expected.data)
        search=self.executable('matrix-search','import sys\n'
            'if "--version" in sys.argv: print("GNU fake search");sys.exit()\n'
            'sys.stdout.buffer.write('+repr(payload)+')\n')
        timer=self.executable('matrix-time','import sys,pathlib,subprocess\n'
            'if "--version" in sys.argv: print("GNU fake time");sys.exit()\n'
            'pathlib.Path(sys.argv[sys.argv.index("-o")+1]).write_text("123 0.00 0.00 1 0\\n")\n'
            'sys.exit(subprocess.call(sys.argv[sys.argv.index("--")+1:]))\n')
        args=argparse.Namespace(fynd=search,fd=search,find=search,time=timer,qualification=None,
            baseline=None,quick=True,repeats=2,timeout=5,threads=[1],fd_threads=[1],
            secondary_root=None,git_tree=[],layout=[])
        out=self.base/'matrix';out.mkdir()
        with patch.dict(os.environ),patch.object(c,'limits',return_value={}),patch.object(c.fixtures,'build',return_value=[self.dataset]),patch.object(c,'cases_for',return_value=[case]),patch.object(c.subprocess,'check_output',return_value='fake ext4'):
            result=c.run(args,out)
        self.assertEqual(result['status'],'SUCCESS')
        self.assertEqual(len(result['validation']),4)
        self.assertEqual(len(result['samples']),8)
        self.assertEqual(len(result['warmups']),2)
        self.assertEqual(len((out/'samples.jsonl').read_text().splitlines()),8)
        reference=out/result['cases'][0]['reference']
        self.assertEqual(reference.read_bytes(),expected.data)
        failures={
            'wrong': 'sys.stdout.buffer.write(b"wrong\\0")',
            'root-mixing': 'sys.stdout.buffer.write('+repr(b''.join(p+b'\0' for p in reversed(expected.paths)))+')',
            'missing': 'sys.stdout.buffer.write('+repr(b''.join(p+b'\0' for p in expected.paths[1:]))+')',
            'duplicate': 'sys.stdout.buffer.write('+repr(payload+expected.paths[0]+b'\0')+')',
            'truncated': 'sys.stdout.buffer.write('+repr(payload[:-1])+')',
            'status': 'sys.stdout.buffer.write('+repr(payload)+');sys.exit(3)',
            'stderr': 'sys.stdout.buffer.write('+repr(payload)+');sys.stderr.write("bad")',
            'timeout': 'import time;time.sleep(10)',
        }
        args.timeout=1
        for mode,body in failures.items():
            Path(search).write_text('#!/usr/bin/env python3\nimport sys\n'
                'if "--version" in sys.argv: print("GNU fake search");sys.exit()\n'+body+'\n')
            rejected=self.base/('rejected-'+mode);rejected.mkdir()
            with self.subTest(mode=mode),patch.dict(os.environ),patch.object(c,'limits',return_value={}),patch.object(c.fixtures,'build',return_value=[self.dataset]),patch.object(c,'cases_for',return_value=[case]),patch.object(c.subprocess,'check_output',return_value='fake ext4'):
                with self.assertRaises(RuntimeError):c.run(args,rejected)
            failure=json.loads((rejected/'failure.json').read_text())
            self.assertEqual(failure['report']['samples'],[])
            self.assertFalse((rejected/'samples.jsonl').exists())

    def test_summary_spread(self):
        rows=[dict(case='x',configuration='tool',elapsed_ns=n*1000000,
              **{key:0 for key in ['cpu_ns','user_cpu_ns','system_cpu_ns','mean_cores','peak_rss_kib','voluntary_switches','involuntary_switches','parent_cpu_ns']}) for n in [1,2,9]]
        summary=c.summary(rows)[0]
        self.assertEqual(summary['median_ms'],2)
        self.assertEqual(summary['n'],3)
        self.assertLess(summary['p10_ms'],summary['median_ms'])
        self.assertGreater(summary['p90_ms'],summary['median_ms'])


if __name__=='__main__':unittest.main()
