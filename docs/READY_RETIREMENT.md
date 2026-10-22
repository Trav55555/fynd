# Completed-task retirement experiment

Decision: NOT_RETAIN. The isolated candidate produces large repeatable package
wins but fails the predefined small-control gate. Production remains the
qualified streaming implementation, `run.vbgYhn`. No full qualification,
promotion, auto mode, skew decomposition, or publication follows this result.

## Change tested

A completed future task can pass an untouched pending-list head. The coordinator
moves the earliest DONE task to the front under PoolMu before selecting any
batch. It rescans after publication wake-ups. Once any batch is selected,
including stderr or an empty flush, that whole task stays pinned until DONE
and all queued output is delivered. Raw LF bytes are never used as framing.

Only DONE tasks move, preserving the relative order of QUEUED worker claims.
`pool.first` remains the actual I/O owner. The N outstanding-task limit and
N MiB aggregate output budget do not change. DONE/unread work remains charged;
credits return only after the actual FILE call, and the task token returns
only after complete delivery. Borrowed path/policy storage survives draining
or abort-and-join. Workers still cannot share descendants, so this is not a
skew-decomposition candidate.

The implementation changes one private transport module and one related
caller comment. A new deterministic C suite checks retirement and pinning.
The existing shared-credit wake test now retires completed C while B is held,
rather than requiring FIFO B-before-C output. Its credit conservation, actual
I/O pinning, and wakeup predicates remain intact.

## Native result and blocking controls

Two fixed waves use the same unchanged application bytes. Each covers 29 cases,
12 datasets, seven configurations, nine repetitions, 203 prechecks, 1,827 native
samples, and 261 warmups. Totals are 406 prechecks, 3,654 samples, and 522 warmups.
Both baseline and candidate are unordered streaming, not a sorted baseline.
Baseline and candidate j1/j8/j32 and fd-auto remain in every case.

The package all-file and none-file j8 medians take about 41-47% less elapsed
time. At j32, the reduction is about 31-36%. All eight primary comparisons
pass the required 15% improvement in both point and paired medians.

Retention is nevertheless blocked by two repeated point-median control flags:

- Empty no-result file at j8: 6.4% and 7.9% slower.
- Tiny all-results file at j32: 8.2% and 8.1% slower.

The table preserves the paired-median counterevidence. Empty paired medians
rise about 4%, below the 5% point threshold. Tiny's second-wave paired median
is below one despite its worse point-median ratio. The predefined control rule
uses repeated point medians, so both flags still block retention. These flags
do not establish the cause or a universal regression. They are not dismissed
as noise, and the threshold is not changed after observing them.

Elapsed times below are milliseconds. Ratios are candidate divided by the
matched qualified baseline. Waves stay separate.

| wave | case | threads | baseline_median_ms | candidate_median_ms | point_median_ratio | paired_median_ratio |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | packages-all-file | 8 | 23.6558 | 13.9135 | 0.5882 | 0.5794 |
| 0 | packages-all-file | 32 | 23.4529 | 16.1572 | 0.6889 | 0.6889 |
| 0 | packages-none-file | 8 | 22.5821 | 12.5909 | 0.5576 | 0.5483 |
| 0 | packages-none-file | 32 | 23.3254 | 14.8784 | 0.6379 | 0.6379 |
| 0 | empty-none-file | 8 | 1.6229 | 1.7263 | 1.0637 | 1.0474 |
| 0 | tiny-all-file | 32 | 2.8189 | 3.0500 | 1.0820 | 1.0730 |
| 1 | packages-all-file | 8 | 22.5650 | 12.3408 | 0.5469 | 0.5232 |
| 1 | packages-all-file | 32 | 22.7729 | 15.2426 | 0.6693 | 0.6967 |
| 1 | packages-none-file | 8 | 21.5191 | 11.4193 | 0.5307 | 0.5196 |
| 1 | packages-none-file | 32 | 21.8384 | 14.3969 | 0.6592 | 0.7024 |
| 1 | empty-none-file | 8 | 1.7678 | 1.9073 | 1.0789 | 1.0406 |
| 1 | tiny-all-file | 32 | 2.6795 | 2.8955 | 1.0806 | 0.9660 |

All other one-wave flags and all 174 case/thread comparisons remain in the
archive, including pipe, serial, raw-name, deep, ignore, and j32 controls.
The complete CSV retains 406 separate distributions with their 13 numeric
metrics. No case, configuration, sample, or blocking flag is dropped.

Fd-auto references are retained, but these isolated gains do not establish
universal fd superiority. Absolute timings are not pooled with the earlier
full benchmark or instrumented loss timelines.

## Behavioral evidence and failures retained

The new test compiles against the unchanged baseline and fails with SIGABRT
at the real FILE-boundary owner assertion, not by timeout or compile error.
With the candidate, it checks early retirement, rescan after wake-up, whole-task
pinning after OUT/ERR/empty flush, raw LF/NUL fragments, list/tail/refill behavior,
slot reuse, conservation, and output-failure joins.

Before timing, seven C suites, 508 CLI checks, 140 ignore checks, and 34 benchmark
tests pass. Sixty repeated concurrency executions and three targeted Clang
ASan/UBSan suites pass. The fast subset/lint checker passes. No required release
profile or 90% per-module coverage floor changes. Full qualification and the new
release sensitivity campaign are NOT run because the native gate blocks them.
The private candidate must not be treated as release-qualified.

The first narrow run preserves a failed old FIFO stream oracle in the shared
credit test. The replacement strengthens controlled completed-task retirement
without deleting the original I/O checks. That run also records a missing
relative c-utils build dependency for CLI invocation, before CLI checks could
run. The first fast check similarly cannot find `cu.h`. An exact private copy
of the immutable archived dependency fixes the checker path. These are preserved
setup/oracle failures, not behavioral mutation kills or suppressed diagnostics.

The final analyzer checks all actual-output receipts, manifests, source/tool
hashes, sample/journal order, numeric counters, GNU-time CPU cross-checks,
summary arithmetic, and the fixed gate. Its coherent CPU-forgery control is
rejected by the unchanged independent timer. It reuses the Python reference
oracle, independent of the measured C but not an independent audit of the
benchmark driver's implementation.

## Limits and current state

All heavy work runs sequentially under MemoryMax=3G, MemorySwapMax=0, nice 15,
and zero core limits including the outer relay. Disk admission requires 5 GiB
free and more than 1.2 million free inodes before a million-file fixture.
Only newly owned fixtures are cleaned. Both native runs complete successfully.
There are 57 and one scope memory-max events respectively, without OOM or swap.
These runs are not memory-pressure-free, controlled-cold, isolated-host, or
cache-residency evidence.

Captured package and skew paths/types are historical metadata replays, with
empty file contents and placeholder links. Selection uses real file and pipe
output captures, never `/dev/null`. Successful raw captures are reused, so
canonical hashes attest runtime validation rather than retrospective rehashing
of every raw stdout. Source and tool hashes are receipts, not signed build or
execution attestations. No first-byte or native CPU-function claim is made.

All 102 selected production inputs and the archived dependency match before and
after. c-utils remains pinned to `eafa795805fa997e2db91f9182c4e0f86fa32fb4`.
HEAD remains `0c7f8a3a72525d51e04dbb124e6eee4c593cfce3`.
Production is unchanged. The candidate and failures remain local for inspection.
No automatic repeat-until-favorable follow-up or further variant starts.
Skew remains a separate unresolved design problem.

## Retained evidence

- [Index](../bench/results/ready-retirement.json)
- [Compressed data](../bench/results/ready-retirement-data.json.gz)
- [Complete unpooled metric CSV](../bench/results/ready-retirement-summary.csv)
- Local plans, sources, tests, logs, paired runs, and gate: `build/ready-retirement/`

The archive retains the rejected gate, all flag lists, source changes, RED and
PASS logs, source bindings, manifests, references, journals, and actual metrics.
It does not package executable binaries or unrelated historical result blobs.
Packed checks require the current hash-bound trusted scripts and benchmark
helpers. Live checks additionally rehash the original run inputs.

```sh
cd /home/travers/projects/fynd
ulimit -c 0
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/ready-retirement/final_check.py --live'
```

No commit, push, installation, dependency update, security/affinity/cache change,
limit increase, or publication occurred. Verified experiment evidence; rejected
candidate, with tiny-control attribution and release qualification unresolved.
