# Serial warm benchmark

From the repository root, use a prebuilt binary under the standard resource caps:

```sh
make bench
make bench BENCH_FYND=/absolute/path/to/fynd BENCH_ARGS='--quick --repeats 2'
make bench-test
```

`make bench` applies 3 GiB, no swap, nice 15, and no core dumps. It uses
`build/fynd` by default and does not rebuild it. Use `make all` first if needed.
No tools are installed. Linux, Python 3, GNU find, and GNU time are required.

The runner can also be invoked directly with resource limits supplied by the caller:

```
python3 bench/bench.py --fynd /absolute/path/to/fynd --output /path/to/results
python3 -m unittest bench.test_bench
```

Options: `--fd` (default `fd`, missing means structured SKIP), `--find`
(default `/usr/bin/find`), `--time` (default `/usr/bin/time`), `--repeats`
(positive, default 5), `--timeout` (positive seconds per invocation, default
120), `--fynd-threads` (1 through 32, default 1), `--quick` (small smoke fixtures). Output is a unique `run-*` directory
under `--output`; it retains stdout, stderr, GNU time RSS files, metadata and
either `report.json` (SUCCESS) or `failure.json` (FAILED). Temporary fixtures
are uniquely owned and removed on exit. No cache dropping, root privileges,
network access, or cold-cache claims.

The deterministic fixtures contain empty regular files and directories, including hidden
names: wide (20,000 files), deep (96 levels × 8 files), mixed (3-way tree,
5 populated levels × 12 files per directory). Quick uses 200 wide files,
12 deep levels and 3 mixed levels. Seed 0 denotes deterministic naming, not a
random generator. Manifests are generated independently during creation.

Both queries select regular files: empty literal and case-sensitive `needle`.
Hidden files and ignored paths are included via fynd's `--no-ignore` and fd's
ignore-bypass flags. Symlinks are not followed; fd uses one configured search
worker. fynd defaults to serial; `--fynd-threads 4` measures four subtree workers
plus its coordinator, not parity with fd's default worker configuration.
The runner compares NUL-delimited **selected-file multisets** using Counter
multiplicity, not ordered traversal parity or sort-then-unique sets. Default
fynd streams are unordered within each root; duplicate and overlapping roots
still repeat records, and root groups must remain in argument order. Dedicated
root-group checks enforce that boundary independently of within-root order.
fd output is also validated by Counter, not traversal order. Every tool/query
must pass validation before any measurement; every measured output is validated
again. Failures, unexpected stderr, wrong record multiplicities, truncated
output, and timeouts reject the run.
Timeouts kill the entire process group. Required executable/version failures
are fatal; fd is skipped only if absent, not if broken or incompatible.

Measurements are serial. Each repetition rotates the shape/query groups and
also rotates the tool order within every group. Creation and validation preheat the cache: these are **warm** results.
Elapsed wall time includes process startup, the GNU time launcher, process wait
and output-file I/O, not pure filesystem walking. RSS is GNU time `%M` in KiB,
not Python-runner RSS. Logs and report include executable hashes/versions,
exact argv, platform/CPU/Python, generation parameters and manifest hashes,
execution order, individual measurements and medians. The runner sets locale C
and rejects executable changes detected by hashing again at the end. No timing-ratio pass/fail
thresholds are applied. Concurrent system activity and filesystem differences
remain uncontrolled; run serially on an otherwise idle machine for comparisons.

The 34 benchmark tests use tiny fake subprocesses (including a deliberately
synthetic RSS timer) and require no installed fd/find. They are correctness
tests, not measurements.

## Comprehensive matrix

`bench/comprehensive.py` extends the small runner with larger scales, thread
counts 1/2/4/8/12/16/32, fd automatic threading, GNU find, selection filters,
ignore pruning and rule counts, captured repository/package layouts, and
validated file/pipe sinks. Optional captured layouts require explicit paths;
the script does not discover or scan arbitrary home directories. Git captures
use immutable commit trees, not concurrent working files. Layout captures copy
paths and kinds into empty-file fixtures, not source contents.

```sh
make bench-full BENCH_FYND=/absolute/path/to/fynd
make bench-full BENCH_FULL_ARGS='--quick --repeats 1 --threads 1 2 --fd-threads 1 2'
```

The target applies the same 3 GiB/no-swap/nice-15/core-0 limits. The runner also
checks the actual kernel limits and refuses uncapped execution. Use an already
built binary; no installs, builds, cache dropping, or production changes occur.
Pass `--qualification /path/to/receipt` to require the exact qualified binary
and matching application sources. `--baseline /path/to/previous/fynd` adds its
j1/j8 reference cases. `--secondary-root /tmp` adds tmpfs fixtures on this host.
Repeated `--git-tree NAME REPO REVISION` and `--layout NAME DIRECTORY` options
capture additional metadata-only layouts. Use `--help` for other arguments.

Each configuration passes a global validation barrier before timing. Every
sample is then validated again against the creation manifest. Independent
sorted DFS remains the canonical manifest/oracle order for expected references
and their hashes, not a promise about actual default-stream output. Actual fynd
and fd records are compared by Counter with exact multiplicity; fynd root-group
order is checked separately and strictly, including duplicate/overlapping roots.
Historical sorting tests describe the retired contract, not a sorting API.
Each case group gets a validated serial warmup. No output is discarded to
`/dev/null`; pipe timing includes draining and capture. Validation follows the
elapsed timer.

To limit disk use, this runner reuses only its own scratch stdout/time files.
It retains per-sample hashes and counts, expected references, compressed
manifests, JSONL samples, exact argv, and failures. It does not delete historical
runs. Fixtures belong to the run and are cleaned after completion. The 3 GiB
limit covers the driver, children, and charged cache; preheating does not promise
full cache residency for the largest datasets.

See [the comprehensive report](../docs/COMPREHENSIVE_BENCHMARK.md) and its
portable data for the actual measurements and comparison constraints.

## Streaming candidate evidence

The native candidate protocol uses two matched warm waves, primarily comparing
serial `-j1` with fd automatic threading on flat-million all-file and all-pipe
cases. First-byte latency, RSS, quiet queries, other thread counts, and
small/deep/branching/shared-prefix controls remain part of the evidence, not
omitted exceptions. This does not promise a universal fd win, an isolated
machine, or cold-cache performance.

Original tiny/quiet 5-11% regression flags remain recorded. The follow-up uses
two waves, 36 repetitions and 12 configurations with the self-alias cleared;
it is not proof of zero regression or that earlier differences were only noise.
Native results do not establish full qualification. Acceptance requires actual
unchanged checker-profile passes, including the six active C test programs and
at least 90% coverage in each module. Retired sorting tests
do not justify weakening those gates.
