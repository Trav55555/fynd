#!/usr/bin/env python3
"""Validated, sequential finder benchmarks. Requires an already capped Linux scope."""
from collections import Counter
from dataclasses import dataclass
import argparse
import contextlib
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


core = load('comprehensive_core', ROOT / 'bench/bench.py')
fixtures = load('comprehensive_fixtures', ROOT / 'bench/comprehensive_fixtures.py')
ASCII_FOLD = bytes.maketrans(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ', b'abcdefghijklmnopqrstuvwxyz')


def limits():
    line = next(x for x in Path('/proc/self/cgroup').read_text().splitlines() if x.startswith('0::'))
    group = Path('/sys/fs/cgroup') / line[3:].lstrip('/')
    result = dict(memory_max=(group / 'memory.max').read_text().strip(),
                  swap_max=(group / 'memory.swap.max').read_text().strip(),
                  nice=os.getpriority(os.PRIO_PROCESS, 0),
                  core_limit=list(resource.getrlimit(resource.RLIMIT_CORE)))
    if result != dict(memory_max='3221225472', swap_max='0', nice=15, core_limit=[0, 0]):
        raise RuntimeError('requires MemoryMax=3G, MemorySwapMax=0, nice15, core0; got '+repr(result))
    return result


@dataclass
class Case:
    dataset: object
    label: str
    pattern: bytes = b''
    kinds: str = 'f'
    hidden: bool = True
    no_ignore: bool = True
    fold: bool = False
    extensions: tuple = ()
    depth: int | None = None
    roots: tuple = (b'',)
    sink: str = 'file'
    baseline: bool = False

    @property
    def ident(self):
        return self.dataset.label + '-' + self.label + '-' + self.sink


class Expected:
    def __init__(self, paths, group_sizes=None):
        self.paths = paths
        self.counts = Counter(paths)
        self.group_sizes = tuple(group_sizes) if group_sizes is not None else (len(paths),)
        if sum(self.group_sizes) != len(paths):
            raise ValueError('root group sizes must cover the independent manifest')
        self.group_counts = []
        offset = 0
        for size in self.group_sizes:
            self.group_counts.append(self.counts if len(self.group_sizes) == 1
                                     else Counter(paths[offset:offset+size]))
            offset += size
        # Canonical sorted DFS is a reference encoding, not a native arrival promise.
        self.data = b''.join(p+b'\0' for p in paths)
        self.sha256 = hashlib.sha256(self.data).hexdigest()

    def validate(self, data, ordered=False, grouped=False):
        if data and not data.endswith(b'\0'):
            raise RuntimeError('truncated or non-NUL output')
        if ordered:
            if data != self.data:
                raise RuntimeError('wrong ordered output (missing, extra, duplicate, or reordered records)')
        else:
            records = data[:-1].split(b'\0') if data else []
            groups = (zip(self.group_sizes, self.group_counts) if grouped else
                      [(len(self.paths), self.counts)])
            offset = 0
            for size, counts in groups:
                if Counter(records[offset:offset+size]) != counts:
                    raise RuntimeError('wrong selected-file multiset or root group (missing, extra, or duplicate records)')
                offset += size
            if offset != len(records):
                raise RuntimeError('extra selected-file records')
        return {'output_sha256': hashlib.sha256(data).hexdigest(),
                'output_bytes': len(data), 'output_records': len(self.paths)}


def validate_output(expected, data, ordered, scratch, grouped=False):
    try:
        return expected.validate(data, ordered, grouped)
    except Exception:
        (scratch/'stdout.bin').write_bytes(data)
        raise


def oracle(case):
    """Manifest-based sorted DFS, independent of every measured implementation."""
    nodes = case.dataset.nodes
    children = {}
    for path in nodes:
        if path:
            children.setdefault(path.rpartition(b'/')[0], []).append(path)
    for paths in children.values():
        paths.sort()
    result = []
    pattern = case.pattern.translate(ASCII_FOLD) if case.fold else case.pattern
    wanted = {ext.translate(ASCII_FOLD) if case.fold else ext for ext in case.extensions}

    def visit(path, depth):
        kind = nodes[path]
        name = path.rsplit(b'/', 1)[-1] if path else os.fsencode(case.dataset.label)
        name = name.translate(ASCII_FOLD) if case.fold else name
        stem, dot, suffix = name.rpartition(b'.')
        if (kind in case.kinds and pattern in name and
                (not wanted or (stem and dot and suffix in wanted))):
            result.append(os.fsencode(case.dataset.label)+(b'/'+path if path else b''))
        if kind != 'd' or (case.depth is not None and depth >= case.depth):
            return
        for child in children.get(path, ()):
            if not case.hidden and child.rsplit(b'/', 1)[-1].startswith(b'.'):
                continue
            if not case.no_ignore and fixtures.ignored(case.dataset, child, nodes[child]):
                continue
            visit(child, depth+1)

    group_sizes = []
    for root in case.roots:
        start = len(result)
        visit(root, 0)
        group_sizes.append(len(result)-start)
    return Expected(result, group_sizes)


def cases_for(datasets):
    cases = []
    for dataset in datasets:
        family = dataset.family
        if family in ('empty', 'tiny'):
            patterns = [('all', b'')]
        elif family in ('git', 'layout'):
            patterns = [('all', b''), ('test', b'test')]
        elif family == 'raw':
            patterns = [('all', b'')]
        else:
            patterns = [('all', b''), ('sparse', b'needle'), ('none', b'not-present-anywhere-987654321')]
        for name, pattern in patterns:
            cases.append(Case(dataset, name, pattern=pattern, no_ignore=family != 'ignore',
                              baseline=family in ('flat','balanced','manydirs','prefix','skew','output','sharedprefix','deep')))
        if family == 'raw':
            cases.extend([Case(dataset, 'visible', hidden=False),
                          Case(dataset, 'ascii-fold', pattern=b'aScIi', fold=True),
                          Case(dataset, 'extension-union', extensions=(b'c', b'txt')),
                          Case(dataset, 'folded-extension', pattern=b'aScIi', fold=True, extensions=(b'c',)),
                          Case(dataset, 'links', kinds='l'),
                          Case(dataset, 'directories', kinds='d', pattern=b'dir'),
                          Case(dataset, 'fifo', kinds='o'),
                          Case(dataset, 'files-links', kinds='fl'),
                          Case(dataset, 'duplicate-roots', roots=(b'',b'')),
                          Case(dataset, 'disjoint-roots', roots=(b'a',b'dir-00'))])
        if family == 'balanced':
            cases.extend([Case(dataset, 'max-depth1', depth=1),
                          Case(dataset, 'max-depth2', depth=2),
                          Case(dataset, 'visible', hidden=False),
                          Case(dataset, 'ascii-fold', pattern=b'nEeDlE', fold=True),
                          Case(dataset, 'extension-c', extensions=(b'c',))])
        if family == 'ignore':
            cases.append(Case(dataset, 'default-visibility', no_ignore=False, hidden=False))
            if dataset.label == 'ignore-prune':
                cases.append(Case(dataset, 'bypass-rules', no_ignore=True))
        if family == 'layout':
            cases.append(Case(dataset, 'extension-py', extensions=(b'py',)))
    pipe_labels = {'flat100k', 'manydirs', 'output', 'ignore512', 'packages'}
    for case in list(cases):
        if case.dataset.label in pipe_labels and case.label == 'all':
            cases.append(Case(**dict(vars(case), sink='pipe', baseline=False)))
    return cases


def arguments(case, path, tool, threads):
    roots = [os.fsencode(case.dataset.label)+(b'/'+p if p else b'') for p in case.roots]
    pattern = case.pattern
    if tool in ('fynd', 'baseline'):
        argv = [path, '-0', '-j'+str(threads)]
        if case.no_ignore: argv += ['--no-ignore']
        if case.hidden: argv += ['-H']
        if case.fold: argv += ['-i']
        for kind in case.kinds: argv += ['-t', kind]
        for ext in case.extensions: argv += ['-e', ext]
        if case.depth is not None: argv += ['--max-depth', str(case.depth)]
        argv += [pattern]+roots
    elif tool == 'fd':
        argv = [path, '-0', '--color', 'never', '--no-ignore-parent',
                '--ignore-case' if case.fold else '--case-sensitive']
        if not case.extensions:
            argv += ['--fixed-strings']
        if threads is not None: argv += ['--threads',str(threads)]
        if case.no_ignore: argv += ['-I']
        if case.hidden: argv += ['-H']
        for kind in case.kinds: argv += ['-t', 'p' if kind == 'o' else kind]
        if case.kinds == 'd':
            argv += ['--format', '{}']  # Avoid fd's directory-only trailing slash.
        if case.extensions:
            # fd -e is case-insensitive even with --case-sensitive. Use its
            # byte-regex filter to preserve the requested suffix semantics.
            if pattern:
                argv += ['--and', b'(?s-u)' + re.escape(pattern)]
            pattern = b'(?s-u)^.+\\.(?:' + b'|'.join(re.escape(e) for e in case.extensions) + b')$'
        if case.depth is not None: argv += ['--max-depth', str(case.depth)]
        argv += [pattern]+roots
    else:
        argv = [path, '-P']+roots
        if case.depth is not None: argv += ['-maxdepth',str(case.depth)]
        prunes = []
        if not case.hidden: prunes += ['-name','.*']
        if not case.no_ignore:
            for name in ['.git','git-drop','plain-drop','excluded','global-drop','local-drop']:
                if prunes: prunes += ['-o']
                prunes += ['-name',name]
        if prunes: argv += ['(', '-type','d','(']+prunes+[')',')','-prune','-o']
        argv += ['(']
        for i, kind in enumerate(case.kinds):
            if i: argv += ['-o']
            argv += ['-type','p' if kind == 'o' else kind]
        argv += [')','-iname' if case.fold else '-name',b'*'+pattern+b'*']
        if not case.hidden: argv += ['!','-name','.*']
        if not case.no_ignore: argv += ['(','!','-name','*.tmp','-o','-name','keep-me.tmp',')']
        if case.extensions:
            argv += ['(']
            for i, ext in enumerate(case.extensions):
                if i: argv += ['-o']
                flag = '-iname' if case.fold else '-name'
                argv += ['(',flag,b'*.'+ext,'!',flag,b'.'+ext,')']
            argv += [')']
        argv += ['-print0']
    return [os.fsdecode(arg) for arg in argv]


def pipe_execute(argv, cwd, scratch, timeout):
    start = time.perf_counter_ns()
    proc = subprocess.Popen(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            start_new_session=True, env=dict(os.environ, LC_ALL='C'))
    try:
        try:
            out, err = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            out, err = proc.communicate()
            (scratch/'stdout.bin').write_bytes(out)
            (scratch/'stderr.bin').write_bytes(err)
            raise RuntimeError('pipe process-group timeout')
    finally:
        if proc.poll() is None: os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
    elapsed = time.perf_counter_ns()-start
    if proc.returncode or err:
        (scratch/'stdout.bin').write_bytes(out)
        (scratch/'stderr.bin').write_bytes(err)
        raise RuntimeError('pipe status/stderr failure: '+repr((proc.returncode,err)))
    return elapsed, out


def invoke(argv, case, scratch, timeout):
    if case.sink == 'pipe':
        return pipe_execute(argv,case.dataset.base,scratch,timeout)
    elapsed = core.execute(argv,case.dataset.base,scratch/'stdout.bin',scratch/'stderr.bin',timeout)
    return elapsed, (scratch/'stdout.bin').read_bytes()


def measure(argv, case, scratch, timer, timeout):
    usage = scratch/'time.txt'
    wrapped = [timer,'-f','%M %U %S %w %c','-o',str(usage),'--']+argv
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    self_before = resource.getrusage(resource.RUSAGE_SELF)
    elapsed, data = invoke(wrapped,case,scratch,timeout)
    self_after = resource.getrusage(resource.RUSAGE_SELF)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    fields = usage.read_text().split()
    if len(fields)!=5: raise RuntimeError('invalid GNU time usage record')
    user = round((after.ru_utime-before.ru_utime)*1e9)
    system = round((after.ru_stime-before.ru_stime)*1e9)
    return data, dict(elapsed_ns=elapsed,user_cpu_ns=user,system_cpu_ns=system,cpu_ns=user+system,
        mean_cores=(user+system)/elapsed,peak_rss_kib=int(fields[0]),
        timer_user_seconds=float(fields[1]),timer_system_seconds=float(fields[2]),
        voluntary_switches=after.ru_nvcsw-before.ru_nvcsw,
        involuntary_switches=after.ru_nivcsw-before.ru_nivcsw,
        parent_cpu_ns=round(((self_after.ru_utime-self_before.ru_utime)+(self_after.ru_stime-self_before.ru_stime))*1e9))


def quantile(values, fraction):
    ordered = sorted(values)
    index = fraction*(len(ordered)-1)
    low = int(index)
    return ordered[low]+(ordered[min(low+1,len(ordered)-1)]-ordered[low])*(index-low)


def save(path, data):
    path.write_text(json.dumps(data,indent=2)+'\n')


def source_identity(qualification, fynd):
    if qualification is None: return None
    receipt = Path(qualification).resolve()
    if (receipt/'exit').read_text().strip()!='0': raise RuntimeError('qualification failed')
    expected = json.loads((receipt/'source-hashes.json').read_text())['fynd']
    selected = {name:digest for name,digest in expected.items()
                if name.startswith(('src/','include/','cmd/')) or name=='CUTILS_REVISION'}
    for name,digest in selected.items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:
            raise RuntimeError('production source drift: '+name)
    accepted = receipt/'source/fynd/build/native/fynd'
    if hashlib.sha256(accepted.read_bytes()).digest()!=hashlib.sha256(Path(fynd).read_bytes()).digest():
        raise RuntimeError('not the exact qualified binary')
    return {'receipt':str(receipt),'source_hashes':selected}


def summary(samples):
    result = {}
    for row in samples: result.setdefault((row['case'],row['configuration']),[]).append(row)
    return [{'case':case,'configuration':config,'n':len(rows),
             'median_ms':statistics.median(x['elapsed_ns'] for x in rows)/1e6,
             'p10_ms':quantile([x['elapsed_ns'] for x in rows],.1)/1e6,
             'p90_ms':quantile([x['elapsed_ns'] for x in rows],.9)/1e6,
             'min_ms':min(x['elapsed_ns'] for x in rows)/1e6,
             'max_ms':max(x['elapsed_ns'] for x in rows)/1e6,
             **{key:statistics.median(x[key] for x in rows) for key in
                ['cpu_ns','user_cpu_ns','system_cpu_ns','mean_cores','peak_rss_kib','voluntary_switches','involuntary_switches','parent_cpu_ns']}}
            for (case,config),rows in result.items()]


def run(args, output):
    scratch = output/'scratch'; scratch.mkdir()
    report = dict(schema='fynd.comprehensive/1',status='RUNNING',limits=limits(),
                  tools={},validation=[],warmups=[],samples=[],order=[],cases=[],datasets=[],
                  load_before=os.getloadavg(),os=platform.platform(),python=sys.version,
                  cpu_affinity=sorted(os.sched_getaffinity(0)),
                  cpu=next(x.split(':',1)[1].strip() for x in Path('/proc/cpuinfo').read_text().splitlines() if x.startswith('model name')),
                  quick=args.quick,repeats=args.repeats,
                  semantics='independent creation-manifest multiset for every output; sequential root groups for native fynd; exact independent DFS bytes only for historical baseline',
                  cache='creation/global prechecks and validated serial warmup before each group; no cache dropping; largest-cache residency not guaranteed under shared3GiB cap',
                  sinks='file includes launcher/writes; pipe includes launcher/draining/capture, validation follows timing; neither is /dev/null',
                  retention='new owned scratch reused; output hashes/counts, expected references, compressed manifests and failures retained',
                  qualification=source_identity(args.qualification,args.fynd),
                  source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in [Path(__file__),ROOT/'bench/comprehensive_fixtures.py',ROOT/'bench/bench.py']})
    save(output/'metadata.json',report)
    tools = {'fynd':core.resolve(args.fynd),'fd':core.resolve(args.fd),
             'find':core.resolve(args.find),'time':core.resolve(args.time)}
    if args.baseline: tools['baseline']=core.resolve(args.baseline)
    for name,path in tools.items(): report['tools'][name]=core.identity(path,'--version',output,args.timeout)
    for name in ['find','time']:
        if 'GNU' not in report['tools'][name]['version']:raise RuntimeError('requires GNU '+name)
    try:
        with contextlib.ExitStack() as stack:
            base = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='fixtures-',dir=output)))
            secondary = None
            if args.secondary_root:
                secondary = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='fynd-comprehensive-',dir=args.secondary_root)))
            home = base/'env'; (home/'git').mkdir(parents=True)
            (home/'git/ignore').write_text('global-drop/\n')
            os.environ.update(HOME=str(home),XDG_CONFIG_HOME=str(home),GIT_CONFIG_GLOBAL=os.devnull,GIT_CONFIG_NOSYSTEM='1')
            print('Creating static owned fixtures...',flush=True)
            datasets = fixtures.build(base,args.quick,args.git_tree,args.layout,secondary)
            references = output/'references'; references.mkdir()
            manifests = output/'manifests'; manifests.mkdir()
            for dataset in datasets:
                filename=manifests/(dataset.label+'.bin.gz');digest=hashlib.sha256()
                with filename.open('wb') as raw:
                    with gzip.GzipFile(fileobj=raw,mode='wb',mtime=0) as compressed:
                        for path,kind in sorted(dataset.nodes.items()):
                            record=kind.encode()+b'\t'+path+b'\0';compressed.write(record);digest.update(record)
                report['datasets'].append(dict(label=dataset.label,base=str(dataset.base),family=dataset.family,
                    policy=dataset.policy,provenance={k:v for k,v in dataset.provenance.items() if k!='captured_paths_manifest'},
                    manifest=str(filename.relative_to(output)),manifest_sha256=digest.hexdigest(),
                    filesystem=subprocess.check_output(['findmnt','-n','-o','SOURCE,FSTYPE','-T',str(dataset.base)],text=True).strip()))
            print('Building independent case oracles...',flush=True)
            groups=[]; expectation_cache={}
            for case in cases_for(datasets):
                key=(case.dataset.label,case.label)
                expected=expectation_cache.setdefault(key,None)
                if expected is None: expected=oracle(case);expectation_cache[key]=expected
                reference=references/(expected.sha256+'.nul')
                if not reference.exists():reference.write_bytes(expected.data)
                report['cases'].append(dict(id=case.ident,dataset=case.dataset.label,pattern_hex=case.pattern.hex(),
                    kinds=case.kinds,hidden=case.hidden,no_ignore=case.no_ignore,fold=case.fold,
                    extensions=[os.fsdecode(x) for x in case.extensions],depth=case.depth,
                    roots_hex=[x.hex() for x in case.roots],sink=case.sink,expected_records=len(expected.paths),
                    expected_bytes=len(expected.data),ordered_sha256=expected.sha256,reference=str(reference.relative_to(output))))
                configs=[('fynd-j%d'%n,'fynd',n) for n in args.threads]
                configs += [('fd-j%d'%n,'fd',n) for n in args.fd_threads]+[('fd-auto','fd',None),('find','find',None)]
                if args.baseline and case.baseline:configs += [('baseline-j1','baseline',1),('baseline-j8','baseline',8)]
                group=[]
                for label,tool,n in configs:
                    argv=arguments(case,tools[tool],tool,n)
                    group.append((case,expected,label,tool,argv))
                groups.append(group)
            save(output/'metadata.json',report)
            print('Precheck barrier:',sum(map(len,groups)),'configurations;',len(groups),'workloads',flush=True)
            for group in groups:
                for case,expected,label,tool,argv in group:
                    report['current'] = dict(stage='precheck',case=case.ident,configuration=label,argv=argv)
                    _,data=invoke(argv,case,scratch,args.timeout)
                    checked=validate_output(expected,data,tool == 'baseline',scratch,grouped=tool == 'fynd')
                    report['validation'].append(dict(case=case.ident,configuration=label,argv=argv,**checked))
            save(output/'metadata.json',report)
            print('All prechecks passed. Native sampling...',flush=True)
            with (output/'samples.jsonl').open('w') as log:
                for repeat in range(args.repeats):
                    order=[]
                    rotate=repeat%len(groups)
                    for group in groups[rotate:]+groups[:rotate]:
                        offset=(repeat*7)%len(group)
                        order.extend(group[offset:]+group[:offset])
                    report['order'].append([case.ident+'/'+label for case,_,label,_,_ in order])
                    previous_case = None
                    for case,expected,label,tool,argv in order:
                        if case.ident != previous_case:
                            warm_argv = arguments(case, tools['fynd'], 'fynd', 1)
                            report['current'] = dict(stage='warmup',case=case.ident,repeat=repeat,argv=warm_argv)
                            warm_elapsed,warm_data = invoke(warm_argv,case,scratch,args.timeout)
                            validate_output(expected,warm_data,False,scratch,grouped=True)
                            report['warmups'].append(dict(case=case.ident,repeat=repeat,elapsed_ns=warm_elapsed))
                            previous_case = case.ident
                        report['current'] = dict(stage='timing',case=case.ident,configuration=label,repeat=repeat,argv=argv)
                        data,metrics=measure(argv,case,scratch,tools['time'],args.timeout)
                        checked=validate_output(expected,data,tool == 'baseline',scratch,grouped=tool == 'fynd')
                        row=dict(case=case.ident,configuration=label,repeat=repeat,**metrics,**checked)
                        report['samples'].append(row);log.write(json.dumps(row)+'\n');log.flush()
                    print('Completed repetition',repeat+1,'/',args.repeats,'samples',len(report['samples']),flush=True)
            report['summary']=summary(report['samples'])
        for name,path in tools.items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=report['tools'][name]['sha256']:
                raise RuntimeError('binary changed: '+name)
        if args.qualification: source_identity(args.qualification,args.fynd)
        for name,digest in report['source_sha256'].items():
            if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:
                raise RuntimeError('benchmark source changed during run: '+name)
        report['load_after']=os.getloadavg();report['status']='SUCCESS'
        save(output/'report.json',report)
        print('PASS',len(report['validation']),'prechecks;',len(report['samples']),'timed validated outputs',flush=True)
        return report
    except BaseException as error:
        save(output/'failure.json',dict(status='FAILED',error=repr(error),report=report))
        raise


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fynd',required=True)
    parser.add_argument('--qualification',type=Path)
    parser.add_argument('--baseline')
    parser.add_argument('--fd',default='fd');parser.add_argument('--find',default='/usr/bin/find');parser.add_argument('--time',default='/usr/bin/time')
    parser.add_argument('--output',type=Path,default=ROOT/'build/comprehensive')
    parser.add_argument('--repeats',type=int,default=9);parser.add_argument('--timeout',type=int,default=120)
    parser.add_argument('--threads',nargs='+',type=int,default=[1,2,4,8,12,16,32])
    parser.add_argument('--fd-threads',nargs='+',type=int,default=[1,2,4,8,12,16,32])
    parser.add_argument('--git-tree',nargs=3,action='append',default=[],metavar=('NAME','REPO','REVISION'))
    parser.add_argument('--layout',nargs=2,action='append',default=[],metavar=('NAME','DIRECTORY'))
    parser.add_argument('--secondary-root',type=Path)
    parser.add_argument('--quick',action='store_true')
    args=parser.parse_args(argv)
    if args.repeats<1 or args.timeout<1:parser.error('positive repeat/timeout required')
    for counts in (args.threads,args.fd_threads):
        if len(set(counts))!=len(counts) or not counts or any(n<1 or n>32 for n in counts):parser.error('distinct thread counts1..32 required')
    limits()  # No uncapped fallback, even on a quick real-tool run.
    args.output.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(args.output).free < (2 if args.quick else 10)*1024**3:
        parser.error('insufficient free space for safe fixture creation')
    output=Path(tempfile.mkdtemp(prefix='run-',dir=args.output)).resolve();print(output,flush=True)
    try:
        run(args,output)
    except BaseException as error:
        failure=output/'failure.json'
        if not failure.exists():
            metadata=output/'metadata.json'
            save(failure,dict(status='FAILED',error=repr(error),
                report=json.loads(metadata.read_text()) if metadata.exists() else None))
        raise
    return 0


if __name__=='__main__':
    raise SystemExit(main())
