# Large-listing memory experiment

[Current streaming](STREAMING.md) subsequently supersedes this storage strategy
by removing complete listing storage. All measurements and decisions below
remain historical.

Historical experiment receipt. The unchanged storage logic was subsequently
[fully qualified and retained locally](LISTING_QUALIFICATION.md) after the tiny
flag did not reproduce in a preregistered follow-up. Original measurements,
flags, source versions and the staged non-retention decision below are preserved.
The original replay guards expect that pre-retention source state; use the new
report's evidence audit on the current working tree.

Separating entry-vector storage from copied names cuts million-file peak RSS
from about 63 MiB to 39 MiB, roughly 38%. Elapsed improvements are modest and
workload-dependent. A tiny threaded control flags the predeclared gate in both
runs, so the candidate remains isolated without full qualification or promotion.
The paired tiny results are noisy; an intrinsic slowdown is not established.

## Production base and initial diagnosis

This work starts from the qualified `run.FyCw0V` production implementation.
None of the rejected continuation changes is included. Their
[optimization line remains stopped](FRONTIER_CONTENTION.md).

The ranked hypotheses were retained vector generations, sorting cost,
per-entry filtering/output cost, and host/probe variation. The pinned library
explains a specific storage issue: CuDir copies names into the listing arena
before CuVec append. Vector growth uses `cu_arena_grow`, which can extend or
reallocate only the newest allocation. Interleaved names prevent that path,
leaving old vector generations allocated until listing release.

A native baseline passes 36 prechecks, 180 timings, and 45 warmups on flat
10k/100k/1m trees, all/quiet file output and all-results pipes. A separate j1
probe adds 12 validated outputs with collection/sort clocks and arena inventory.
It measures one listing, not concurrent worker phases.

On one million entries, the probe reports:

| Quantity | Bytes |
| --- | ---: |
| Listing arena held allocation | 65,838,816 |
| Current entry-vector capacity | 25,165,824 |
| Copied names and root path | 15,247,761 |
| Held minus current capacity and names | 25,425,231 |

The last row includes slack, headers and padding as well as old vector storage;
it is not an exact allocator-waste measurement. These are held-allocation
counters, not resident-memory counters. The native peak RSS independently
reproduces roughly 63 MiB.

Diagnostic collection thread CPU is about 283 to 290 ms and sorting about
124 to 144 ms on that fixture. Collection excludes listing initialization;
the probe's total spans collection through sorting, including its inventory
loop and clock overhead. Sorting excludes that loop. These spans are perturbed,
not native CPU fractions. Neither first externally observable result latency
nor controlled-cold/cache-resident behavior was measured.

fd auto uses roughly 6 to 15 MiB across the million-file quiet/all sinks in
this baseline. fd j1 uses roughly 268 to 273 MiB. Those configuration-specific
observations do not support a general claim that fd always uses less memory.
fd ordering and threading semantics also differ.

## One gated storage change

`ListingJob` gains a stable record arena. Lists through 1,024 entries retain
the existing name/vector allocation path; initializing the unused arena allocates
nothing. On append 1,025, a temporary CuVec reserves checked capacity on the
record arena and copies the first 1,024 records. The CuVec control transfers to
`job.entries`; neither arena moves. Subsequent growth has no intervening name
allocations and can use the pinned allocator's tail-growth path.

Copied names and the listing path remain in their original arena. Existing
sorting, unsigned-byte order, kind/eligibility payloads, caller iteration,
ignore/hidden/depth policy and single-pass directory reading are unchanged.
No reopened enumeration, dependency edit, custom slab/index container, new CLI
option, or output/traversal scheduler is introduced.

The collector tracks append failure explicitly and checks both sticky arena
flags plus vector failure. This matters at migration: failed reservation leaves
the old vector in a healthy name arena. Ignoring the new owner failure could
incorrectly accept a partial listing. Errors still prevent sorting and child
processing. Release frees record storage and then names after borrowed work
has drained or joined.

The N-job/DONE/delivery and N MiB output bounds are unchanged. Each listing has
additional arena metadata; larger listings retain a bounded amount of old small
vector storage. This is not a whole-process RSS cap. Small lists keep their old
allocation requests but perform extra arena initialization/cleanup metadata work;
code generation and layout can also change.

## Sensitive local verification

The initial strict-compiled test creates 0/1/1023/1024/1025/8193-entry listings.
It validates counts, fixed-width independent ordering, names/kinds/payloads,
path stability and cleanup. After deleting every owned fixture, it asserts a
normal-arena allocation budget based on current capacity plus names, two block
slacks and header allowance. Production exceeds that budget and aborts with
status 134; the candidate passes. This is an exercised fixture budget, not RSS
or whole-program memory proof. Debug arenas explicitly exclude that budget
because of their per-allocation headers.

Added cases independently fail record migration and later growth, exercise a
zero record-arena limit and name-copy failure, check no sorting on errors, and
verify both arenas have zero held/cached bytes after release. Failed migration
is checked while the original vector remains healthy. Six C executables,
508 CLI checks, 140 ignore checks, and targeted Dependable C fast checks pass.
The original red source and strengthened green source/logs are retained.

Source review found no material implementation bug. Missing cases include name
allocation failure after migration and record-arena ENOMEM through worker
ordered drain/abort, including discarded children and joined cleanup. Full
sanitizer/platform/coverage/semantics/fuzz/mutation qualification was not run.
Existing required profiles and 90% per-module floor remain unchanged.

## Two matched native matrices

Each run has 21 cases, nine configurations, nine balanced rotated repetitions,
189 prechecks, 1,701 timings, and 189 warmups. Production/candidate j1/4/8/16 and
fd auto are co-run. Cases include flat 10k/100k/1m all/quiet/file/pipe, threshold
1024/1025, empty/tiny, deep, branching and shared-prefix controls. Every output
matches independent creation manifests, exact DFS bytes for fynd and multisets
for fd. Timings use actual file or captured-pipe output.

Million-file medians:

| Workload | Run 1 production ms | Run 1 candidate ms | Run 2 production ms | Run 2 candidate ms |
| --- | ---: | ---: | ---: | ---: |
| All, file, j1 | 616.48 | 584.85 | 591.64 | 572.44 |
| Quiet, file, j1 | 507.91 | 504.84 | 506.84 | 491.94 |
| All, pipe, j1 | 562.33 | 559.68 | 563.48 | 549.32 |
| All, file, j8 | 624.94 | 593.25 | 583.33 | 577.29 |
| Quiet, file, j8 | 510.54 | 508.97 | 511.90 | 490.48 |
| All, pipe, j8 | 592.95 | 562.87 | 570.18 | 565.02 |

All-results j1 peak RSS falls 62.91 to 38.69 MiB and 62.81 to 38.82 MiB.
At j8 it falls 63.00 to 38.94 and 63.06 to 39.07 MiB. The predeclared 20%
RSS-reduction alternative passes in both runs across tested thread counts.
The 15% elapsed alternative does not pass; latency gains are smaller.

### Unresolved tiny-control flag

The gate also disallows repeated elapsed regressions above 5% on controls.
Five-file all-results j8 medians are 1.702 to 1.856 ms and 1.769 to 2.020 ms,
about 9.10% and 14.18% higher, or 0.155 and 0.251 ms. This is the only repeated
point-median breach across the tested case/thread configurations. It blocks
promotion under the declared rule; the memory target is not substituted for
passing every control.

Paired ratios tell a less decisive story. Their medians are 0.995 and 1.135,
with exploratory nominal 95% bootstrap intervals 0.880 to 1.157 and 1.001 to
1.292. These differ from ratios of independent medians. The intervals include
the 5% threshold, and the first run does not show a paired-median slowdown.
The evidence does not establish an intrinsic tiny-path penalty. A focused
paired control check is needed before full qualification; no speculative code
fix or gate relaxation is made here.

Native matrix load changed about 7.97 to 6.66 and 6.66 to 7.14. Shared-host,
filesystem, startup, scheduling and layout effects remain possible confounders.
No source-specific CPU or host-noise attribution, universal speedup, first-result
latency improvement, or fd parity is claimed. Historical results are unchanged.

## Evidence and decision

NOT_RETAINED_NATIVE_CONTROL_FLAG. Candidate code remains under
`build/large-listing/candidate`; production is unchanged. No commit, push,
install, publication, dependency, security, affinity, or cache change occurred.
Only newly owned fixtures were cleaned.

[Portable index](../bench/results/large-listing.json),
[full compressed evidence](../bench/results/large-listing-data.json.gz), and
[summary CSV](../bench/results/large-listing-summary.csv) retain the measured
native/diagnostic records, source/test versions, driver/helpers, red/green logs,
identities, plans, and audit/replay scripts. Local runs are
`build/large-listing/profile-dxv4ilug`, `run-kw3lhuve`, and `run-wz80he0b`.

Inventory totals are 414 prechecks, 3,582 native timed outputs, 423 warmups,
and 12 diagnostic outputs, kept in separate distributions. CSV has 414 summary
rows, 378 matched-matrix rows plus 36 baseline-profile rows. Audits check JSONL,
counts/bytes/exact fynd hashes, distributions, balanced matrix order, final
source/tool identities, independent GNU time, archive/CSV agreement and cleanup.
Coherently doubled CPU/user/system/cores fails the independent timer. fd raw
bytes passed multiset validation before scratch reuse; scalar hashes cannot
recreate those bytes afterward. Same-family reviewers did not independently
rehash raw evidence.

All heavy work ran sequentially under actual 3 GiB/no-swap/nice-15/core-0 limits
with a 5 GiB disk preflight and inode headroom checks for million-file fixtures.
No full-residency/cold-cache, native hardware-attribution, total RSS-cap or
whole-program filesystem/scheduling proof is claimed.

Local replay requires retained binaries and pinned paths, not an installer:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/large-listing/experiment.py --offset 0'
```

Partially verified. The measured memory reduction and control flag are verified;
the experimental implementation is not qualified for release.
