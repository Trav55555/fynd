# Separating cancellation reads from scheduler work

One focused attribution pass identified cancellation polling as the largest
mutex-acquisition category in the reclamation candidate. Giving each task slot
a separate read guard improves some j8 controls, but repeated j16 smaller-tree
regressions fail the unchanged gate. Production remains unchanged. Continuation
optimization stops here; the next priority is million-file listing latency and
memory, not another scheduler variant.

## Attribution before the change

The base is [the reclamation candidate](FRONTIER_RECLAIM.md), not the subsequent
[one-poll subtraction](FRONTIER_POLL.md). Ranked hypotheses were cancellation
reads contending with scheduler work, frame/control management, output handoffs,
and insufficient discrimination because of instrumentation or shared-host noise.

Two diagnostic builds wrap explicit CuMutex lock/unlock and CuCond waits using
bounded per-thread records. One build counts without clocks. The other samples
every sixteenth acquisition per thread/site and records wait/hold wall and thread
CPU spans. Condition waits close the hold segment and reopen it after return;
parking and inseparable internal mutex reacquisition are recorded separately.
They are never counted as explicit mutex wait or hold.

The profile has four small/balanced all/quiet cases, three repeats per mode,
and 24 independently validated diagnostic outputs. Each case has exactly 37,320
poll acquisitions, about 80% of roughly 46,000 explicit acquisitions. Major
frame/control sites each contribute about 1,500 to 1,700 acquisitions. Polling
persists with no matches, so output handoffs cannot explain that count.

For roughly 2,330 sampled poll acquisitions, aggregate sampled wait-wall medians
are about 4 to 6 ms and hold-wall about 1.6 ms. No extrapolation is made.
Clocks, wrappers, TLS lookup, and sampling alter lock holding and scheduling.
These are not native CPU fractions or proof that all remaining overhead comes
from cancellation. The diagnostic wrapper assumes at most one held mutex and
profiles only the unmodified base. It cannot instrument the new nested writer
locking unchanged.

Five-repeat native controls add eight prechecks, 40 timed outputs, and 20
warmups. They remain separate from the two full matrices below.

## The single synchronization change

Every existing poll remains, including the post-classification poll removed in
the rejected subtraction experiment. `Task` gains a stable `CuMutex *poll_mutex`
for its slot. A polling reader locks only that guard, copies the same
`pool->stop` flag, unlocks, and returns. There is no cached or duplicated flag,
volatile access, lockless read, C11 atomic, new asynchronous actor, or reduced
polling frequency.

Scheduler readers still hold the scheduler mutex. The running writer, abort,
holds that mutex and every initialized slot guard in ascending order while
setting stop. It releases guards in reverse. Every conflicting reader therefore
shares a mutex with the writer, even though readers use different guards.
Native guard-held readers never acquire another lock, wait on a condition,
perform I/O, or call cancellation. Initialization sets zero before thread starts.

All 32 guard pointers are initialized before fallible work. Used guards are
created after the scheduler mutex and before conditions or workers. Partial
startup locks/destroys only successful guards. Stop and joins precede frontier
cleanup; conditions, guards, scheduler mutex, and arena unwind in reverse order.
Guards remain stable through slot reuse and continuation splitting. Serial
and j1 still start no primitives.

The N-job bound including DONE/current delivery, N MiB output payload, control
caps, root admission, output policy, and borrowed-storage lifetimes are unchanged.
There are additional O(N) mutex objects and pointer metadata outside payload;
this is not a total memory/RSS-cap claim.

## Sensitive checks

Before implementation, a strict-compiled test calls the actual stop-read path
while Main holds the scheduler mutex. A private hook rejects that mutex as its
read guard and aborts with status 134 before blocking. The same test passes
afterward: helper reads complete while Main retains the scheduler mutex, slots
use distinct guards, reads observe stop after abort, repeated aborts preserve
join counts, and a bounded startup-failure sweep cleans the pool arena.

The j2 primitive-failure sweep expands from six to eight attempts for the two
new guards. Default serial and explicit j1 fail-at-zero assertions remain armed.
Nine C executables, 508 CLI checks, 140 ignore checks, and targeted Dependable C
fast checks pass. Existing tests were not weakened. Same-family source review
found no material correctness bug before native testing.

Concurrent writer-exclusion sensitivity, full TSan/other required profiles,
complete peak listing/control bounds, and the preceding continuation ownership
and fault gaps remain unqualified. Code inspection and post-abort reads do not
prove every concurrent writer schedule. No full release qualification follows
a failed native gate.

## Matched native results

Each matrix contains 16 cases, ten configurations, and ten rotated repetitions.
Production and candidate j1/4/8/16, preceding reclamation j8, and fd run in the
same groups. Together: 320 prechecks, 3,200 timed-output validations, and 320
separate warmups. Exact fynd DFS bytes and fd multisets match independent
manifests. Every timing validates actual file or captured-pipe output.

Median j8 elapsed milliseconds, file sink:

| Workload | Run 1 production | Run 1 preceding | Run 1 guards | Run 2 production | Run 2 preceding | Run 2 guards |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Smaller tree, all | 19.64 | 23.18 | 21.61 | 19.47 | 21.02 | 19.60 |
| Smaller tree, quiet | 18.55 | 19.45 | 18.63 | 18.37 | 19.63 | 18.53 |
| Balanced, all | 50.08 | 40.42 | 43.31 | 50.12 | 39.68 | 37.33 |
| Balanced, quiet | 31.73 | 34.08 | 31.70 | 32.12 | 33.93 | 30.92 |
| Dominant branch, quiet | 91.43 | 44.57 | 40.99 | 93.89 | 42.11 | 40.11 |
| Extra-level skew, quiet | 92.47 | 49.78 | 50.10 | 93.95 | 50.18 | 50.02 |

Both quiet skew shapes exceed the required 15% gain in both runs. Quiet smaller
j8 medians are within 1% of production in both runs. Smaller all-results j8 is
10.01% slower in the first run and 0.67% slower in the second, not a repeated
5% breach by that configuration. Balanced j8 controls mostly improve.

The preceding candidate was co-run only at j8. Its quiet-smaller CPU medians fall
from 102.30 to 99.47 ms and 103.04 to 99.91 ms with the guards, about 3%.
That does not establish a native CPU percentage attributable to a particular
sampled lock span.

### Why this candidate is still rejected

The controls include the tested thread configurations, not only j8. At j16:

| Workload | Run 1 production | Run 1 guards | Run 2 production | Run 2 guards |
| --- | ---: | ---: | ---: | ---: |
| Smaller tree, all | 20.46 | 25.37 | 20.15 | 23.25 |
| Smaller tree, quiet | 18.81 | 20.64 | 18.55 | 22.48 |

All-results regressions are 23.99% and 15.38%; quiet regressions are 9.68% and
21.22%. These repeated median differences fail the unchanged 5% control gate.
No thread configuration is dropped to turn that failure into acceptance.
The predecessor's j16 binary was not co-run here, so these comparisons do not
isolate which part of the continuation design causes its j16 penalty.

The gate remains the predeclared repeated-median rule. Bootstrap summaries are
exploratory and do not establish isolated-host or universal performance.
Native matrix load changed about 3.62 to 6.37 and 6.37 to 5.17. Host timing,
filesystem output, layout, and scheduling effects remain possible confounders;
no single hardware or host-load attribution is claimed.

## Decision, evidence, and boundaries

REJECTED_NO_PROMOTION. This is the final continuation attempt in the approved
investigation. No further synchronization variant or automatic thread cap is
introduced. Full qualification and missing ownership/fault/listing evidence
would be required for any future retention; required profiles and the 90%
per-module coverage floor remain unchanged.

All 120 captured production inputs still match, including the 100 selected
qualification inputs. No commit, push, install, publication, dependency edit,
security-policy, affinity, or cache change occurred. Only newly owned fixtures
were cleaned; historical evidence and failures remain.

[Portable index](../bench/results/frontier-contention.json),
[full compressed evidence](../bench/results/frontier-contention-data.json.gz),
and [matrix summary CSV](../bench/results/frontier-contention-summary.csv)
retain diagnostic raw/site records, native samples, measured drivers/helpers,
source/test versions, red/green logs, identities, and audit/replay scripts.
Local runs: `build/frontier-contention/profile-emu8y5ar`,
`run-4hbubivd`, and `run-5o29fvgu`.

Across the separate profile and matrices there are 328 prechecks, 3,240 native
timed outputs, 340 warmups, and 24 diagnostic outputs. These totals are inventory,
not pooled timing distributions. The CSV contains 320 matrix summary rows; the
focused native summary is embedded in compressed evidence.

The audit checks JSONL/report equality, distributions, counts/bytes/exact fynd
hashes, order balance, CPU arithmetic and independent GNU time, final tool/source
identities, archive/CSV agreement, and cleanup. A coherent doubled-CPU record
fails the independent timer. fd raw bytes passed multiset validation before
scratch reuse; scalar hashes cannot recreate those bytes afterward. Reviewers
use the same model family and do not independently rehash raw evidence.

All heavy work ran sequentially under actual 3 GiB/no-swap/nice-15/core-0 limits
with a 5 GiB disk preflight. No controlled-cold/full-residency, slow-storage,
total RSS cap, native perf attribution, whole-program scheduling proof, or
universal fd parity is claimed.

Local replay requires retained binaries and pinned paths, not an installer:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/frontier-contention/experiment.py --offset 0'
```

The subsequent [large-listing experiment](LARGE_LISTING.md) starts from qualified
production, excluding all continuation changes. It reduces million-file RSS
about 38%, but a noisy tiny control flags the native gate. It remains isolated.

Partially verified. The synchronization-route change and native rejection are
verified; this experimental continuation implementation is not release-qualified.
