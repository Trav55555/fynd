# Benchmarks and historical serial baseline

The [full streaming benchmark](STREAMING_FULL_BENCHMARK.md) covers 107 workloads
and 16,002 validated timed outputs. Fixed j8 has lower point medians than fd-auto
on 81/86 primary workloads and 18/21 matched controls. Python package layout and
dominant-branch skew remain important losses. These counts are not significance tests.

The initial [qualified unordered-streaming result](STREAMING.md) makes serial
fynd take about 23% less time than fd-auto on the matched warm million-file
file-output case and about 27% less on its pipe case. Output order within a root is now
unspecified, and late read errors retain earlier results. These are scoped wins,
not universal fd superiority.

For the broader historical comparison, see [the comprehensive benchmark](COMPREHENSIVE_BENCHMARK.md).
It includes larger trees, thread scaling, ignore rules, captured layouts, pipelines,
and matched filesystem controls. Earlier narrow wins do not establish general parity.

The earlier retained change was [gated large-listing storage](LISTING_QUALIFICATION.md):
about 63 to 39 MiB million-file peak RSS, followed by focused tiny validation and
full qualification. Latency gains remain modest and workload-dependent.
A later [first-byte latency investigation](LISTING_LATENCY.md) rejects one
prefix-aware sort candidate: roughly 5-6% primary gains miss its 15% gate.
That rejected experiment left production unchanged at the time; streaming now
supersedes its sorted-listing baseline.
A subsequent [collection-cost attribution](COLLECTION_COST.md) finds no justified
application candidate for the same latency target. Its diagnostic user/system
splits and Callgrind edges are separate from native CPU fractions.

For the earlier retained changes, see [shared output credits and ignore rejection](PERFORMANCE_PASS.md).
The paired follow-up improves large output-rich trees and literal-leading ignore
rules, with a smaller-tree j8 regression and explicit 64/256 KiB batch tradeoffs.
A [narrower regression investigation](REGRESSION_INVESTIGATION.md) did not reproduce
the earlier large smaller-tree or balanced-serial regressions. Its two matched
waves are separate evidence, not replacements for the original matrix.

The later [bounded skew prototype](SKEW_TRAVERSAL_EXPERIMENT.md) was rejected.
It helps one dominant branch but not the extra-level skew control, and misses
its smaller-tree regression gate. Production is unchanged by that experiment.
The [continuation alternative](FRONTIER_TRAVERSAL_EXPERIMENT.md) redistributes the
deeper branch, but was also rejected because smaller and quiet controls regress.
A [profile-driven private-leaf reduction](FRONTIER_COST.md) cuts some CPU and
context switching, but still fails those controls. It is not retained.
The next [disposal-only reduction](FRONTIER_RECLAIM.md) moves allocator cleanup
outside the scheduler mutex. Smaller all-results timings approach production,
but quiet-small medians remain 6-7% slower. It also stays isolated.
Removing [one redundant cancellation poll](FRONTIER_POLL.md) saves the expected
work but does not pass the same native controls. Production remains unchanged.
A final [contention investigation](FRONTIER_CONTENTION.md) separates cancellation
reads from scheduler work. Quiet j8 controls approach production, but repeated
j16 smaller-tree regressions reject it. Continuation optimization stops here.
The next [large-listing experiment](LARGE_LISTING.md) reduces million-file peak
RSS about 38%. Its initial tiny j8 gate flag remains historical; a preregistered
follow-up did not reproduce it with unchanged application code. The change is
[now qualified and retained](LISTING_QUALIFICATION.md), without including any
continuation prototype.

The earlier [coarse traversal results](COARSE_TRAVERSAL.md) explain the sibling
walker. It does not rebalance skewed subtrees and can retain more ancestor memory.

The earlier [sort optimization](SORT_OPTIMIZATION.md) improved flat-directory scans;
long shared filename prefixes regressed slightly.

The earlier [scheduling experiment](SCHEDULING_EXPERIMENT.md) rejected an early-refill
deletion: it increased worker activity but regressed branching workloads.
Production code was unchanged by that experiment.

For the retained paired before/after changes, see
[walk optimization results](WALK_OPTIMIZATION.md). They isolate redundant path
work and queue notifications, with all historical baselines retained.

For the newer 1/2/4/8-worker comparison on larger ext4 fixtures, see
[the full matrix](BENCHMARK_MATRIX.md). It includes fd with automatic threading;
the earlier single-worker-only comparisons below are not equivalent to that.

The baseline validates regular-file selections against an independent creation
manifest, then compares warm-cache elapsed time and peak RSS. It does not test
full fd/GNU find compatibility or compare their default configurations.

## Reproduce

```sh
make all
make bench-test
make bench
```

For the quick smoke fixtures:

```sh
make bench BENCH_ARGS='--quick --repeats 2'
```

`make bench` uses a prebuilt binary without rebuilding it. To measure a binary
from a successful pinned verification run:

```sh
make bench BENCH_FYND=build/verification/run.XXXXXX/source/fynd/build/native/fynd
```

The target applies 3 GiB, no swap, nice 15, and no core dumps. It never drops
kernel caches or installs tools. See [the procedure](../bench/README.md) for
arguments, fixture generation, validation, and raw artifact layout.

## Original serial baseline

This run predates the ignore/thread implementation. Its source hashes remain
in the preserved result data; use the later comparison below for current code.

- Local artifacts: `build/bench/run-5f3z_wp2`, exit 0.
- [Portable result data](../bench/results/serial-baseline.json) includes every
  sample, execution order, tool versions/hashes, manifest hashes, and build
  provenance. Machine-specific absolute paths are omitted from this copy.
- Build: the non-instrumented `-O2 -g` native binary from accepted verification
  `run.wVWrW0`, using pinned c-utils
  `eafa795805fa997e2db91f9182c4e0f86fa32fb4`.
- Tools: fynd 0.1.0, fd 10.3.0 with one configured search worker,
  GNU find 4.10.0, GNU time reporting version `UNKNOWN`. The timer's binary
  hash is recorded even though its version string is incomplete.
- Host: AMD Ryzen 5 7600, Linux 6.17 x86-64, glibc 2.42. Other host activity
  was not controlled; one-minute load average was about 4.06 before and after.
- Five repetitions for each of 18 shape/query/tool combinations: 90 measured
  executions after all 18 independent prechecks passed. Every measured output
  was validated again. There were no missing tools or skipped comparisons.

Fixtures contain zero-byte regular files and directories, including hidden
names. The wide tree has 20,000 files in one directory. The deep tree has
768 files in 97 directories, through depth 96. The mixed tree has 1,452 files
in 364 directories. Each runs an all-files query and the case-sensitive
basename substring `needle`, which selects 1,819, 96, and 242 files respectively.

### Median elapsed milliseconds

| Shape/query | fynd | GNU find | fd, one worker |
| --- | ---: | ---: | ---: |
| Wide, all | 11.200 | 12.386 | 19.890 |
| Wide, needle | 10.606 | 12.322 | 18.670 |
| Deep, all | 2.928 | 3.906 | 6.147 |
| Deep, needle | 2.546 | 3.499 | 4.598 |
| Mixed, all | 4.101 | 5.825 | 6.316 |
| Mixed, needle | 3.981 | 5.693 | 6.177 |

Elapsed time includes process startup, the GNU time launcher, output-file I/O,
and runner bookkeeping. It is not pure walking time. Completion is observed
through a stderr pipe rather than polling with `wait(timeout)`, whose sleep
intervals would distort these short runs.

### Median peak RSS, KiB

| Shape/query | fynd | GNU find | fd, one worker |
| --- | ---: | ---: | ---: |
| Wide, all | 3,380 | 8,116 | 12,980 |
| Wide, needle | 3,508 | 8,216 | 12,980 |
| Deep, all | 1,716 | 2,856 | 4,660 |
| Deep, needle | 1,716 | 2,956 | 4,148 |
| Mixed, all | 1,588 | 2,580 | 4,916 |
| Mixed, needle | 1,564 | 2,684 | 4,020 |

RSS is the child process's GNU time measurement, not the Python driver's heap
and not a claim about allocator bytes alone.

## Interpretation and limits

fynd had the lowest median elapsed time and peak RSS in these six selected
workloads. That is a narrow observation on one host, not a fastest-tool claim.
The numbers do not establish parity with default parallel fd, cold-cache
searches, network filesystems, huge directories, or real repositories.

All tools include hidden entries and ignore no files for this comparison.
Results are compared as NUL-delimited multisets. fynd still pays for its
sorted preorder; the other tools are not forced to reproduce that order.
Creation and correctness checks preheat the cache. Tool order rotates within
each workload and workload order rotates between repetitions. No timing ratio
can make verification pass or fail.

This original evidence did not justify enabling parallelism automatically.
The later opt-in implementation and its measured costs are recorded below.

## Ignore/thread implementation: one versus four workers

Both configurations used the accepted non-instrumented binary from
`build/verification/run.2Qgzaa`, the same deterministic fixtures, five repetitions,
and the same resource caps. `--no-ignore` aligns file selection with the reference
tools. The four-worker case also has a coordinator; fd still uses one worker.
These are separate sequential runs, not randomized configuration-order trials.

```sh
make bench BENCH_FYND=build/verification/run.2Qgzaa/source/fynd/build/native/fynd BENCH_ARGS='--fynd-threads 1'
make bench BENCH_FYND=build/verification/run.2Qgzaa/source/fynd/build/native/fynd BENCH_ARGS='--fynd-threads 4'
```

[Portable samples and provenance](../bench/results/ignore-threads-baseline.json)
preserve all 180 measurements and 36 prechecks. Local artifacts are
`build/bench/run-6xjpes4j` and `build/bench/run-f5rm41ui`. All selected-file
validations passed with no skipped tools.

| Shape/query | One worker, ms | Four workers, ms | One, RSS KiB | Four, RSS KiB |
| --- | ---: | ---: | ---: | ---: |
| Wide, all | 10.550 | 11.029 | 4,284 | 4,284 |
| Wide, needle | 10.757 | 10.727 | 4,156 | 4,284 |
| Deep, all | 3.453 | 4.155 | 2,620 | 2,108 |
| Deep, needle | 3.118 | 4.277 | 2,620 | 2,236 |
| Mixed, all | 4.752 | 5.075 | 1,724 | 1,852 |
| Mixed, needle | 3.850 | 4.250 | 1,724 | 1,852 |

Values are medians. Four workers were generally slower here. The 0.030 ms
wide-needle difference is not treated as an established speedup. Wide directories
with only regular files offer no independent child listings, and single-chain
deep trees offer little overlap. Thread startup and synchronization are included.

Serial therefore remains the default. These warm fixtures do not establish
behavior on cold, high-latency, or network storage. Profile and broaden workloads
before changing defaults or parallelizing additional stages.
