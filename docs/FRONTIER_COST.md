# Continuation cost diagnosis and rejected leaf reduction

Profiling points to added synchronization and management activity, not additional
filesystem traversal. A task-private leaf path reduces some of that cost, but
still fails the retention gate. Production remains unchanged.

## Evidence before the change

The earlier [continuation prototype](FRONTIER_TRAVERSAL_EXPERIMENT.md) reproduced
large smaller-tree CPU regressions in two runs. A narrower profile then measured
12 native repetitions per configuration on the same 37,324-file tree, followed
by separate Callgrind and syscall probes. Every actual output was validated.

Quiet j8 medians from `profile-n5ss6kcc`:

| Configuration | Native elapsed, ms | Native CPU, ms | Native voluntary switches | Traced futex calls | Callgrind instructions |
| --- | ---: | ---: | ---: | ---: | ---: |
| Production | 18.21 | 90.72 | 61 | 104 | 54.997 million |
| Continuation prototype | 28.31 | 128.32 | 5,991 | 12,493 | 63.494 million |
| Diagnostic split-disabled variant | 25.93 | 111.76 | 3,437 | 5,902 | 62.686 million |

Each trace has the same 18,662 getdents64, 9,334 openat, 9,334 close, and 9,333
fstat calls. Native CPU rises about 41% in this focused run. The prior matrix's
roughly 36% increase is separate evidence, not the same statistic.

Disabling splits preserves much of the penalty, so fragment handoffs alone do
not explain it. The no-match case retains the penalty, weakening output transport
as the main explanation. Allocator instruction growth is modest in these
Callgrind records. Extra per-directory frame/cursor locking is the first targeted
reduction, not a claim that every added microsecond has been attributed.

Callgrind counts userspace instructions; strace perturbs scheduling and syscall
cost. Their elapsed times and percentages are not native CPU or latency fractions.
Inclusive Callgrind costs overlap and must not be summed as exclusive work.
Native resource accounting and context switches are separate measurements.

## One targeted change

The candidate keeps one exclusively leased scratch Frame control per task slot.
It builds and filters a complete directory once at that stable address. If there
is no eligible descending child, it processes the entries synchronously and
releases all listing/rule storage outside the scheduler mutex. The empty control
can serve the next directory. No leaf listing enters the shared continuation graph.

With any descending child, the same Frame promotes under the mutex after cursor
allocation succeeds. Only then does it acquire a parent reference and become
persistent. It is not copied, reopened, or rematched. Cutoff directories and the
default depth-128 diagnostic still follow the existing selection/guard policy.
The shared/private controls together use the existing control cap. Task tokens,
N MiB staging, reservations, splitting policy, root groups, and CLI did not change.

A compile-valid regression test checked exact bytes and status before asserting
zero persistent leaf publications. On the old prototype it observed one
publication and aborted with status 134. After the change, it passes. This is a
behavioral failure, not a compilation failure or timeout. The seven C tests,
508 CLI checks, 140 ignore checks, and targeted fast checks also pass.

## Matched before/after measurements

Two new independent matrix runs include production, the unmodified continuation
binary, and the candidate in the same fixture groups. Each has 16 cases, ten
configurations, and ten rotated repetitions: 320 prechecks, 3,200 timed outputs,
and 320 validated warmups across both runs.

Median j8 values; "B" means the unmodified continuation prototype:

| Workload/metric | Run 1 production | Run 1 B | Run 1 candidate | Run 2 production | Run 2 B | Run 2 candidate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Smaller tree, all, elapsed ms | 19.51 | 31.73 | 27.44 | 19.37 | 30.29 | 29.98 |
| Smaller tree, all, CPU ms | 93.56 | 127.37 | 119.31 | 94.16 | 129.78 | 122.19 |
| Smaller tree, quiet, CPU ms | 91.06 | 126.26 | 117.98 | 92.09 | 127.42 | 119.07 |
| Balanced, quiet, elapsed ms | 34.49 | 41.49 | 40.18 | 33.53 | 41.70 | 40.06 |
| Dominant branch, quiet, elapsed ms | 93.53 | 46.06 | 45.00 | 92.31 | 46.09 | 47.93 |
| Extra-level skew, quiet, elapsed ms | 91.08 | 52.01 | 51.19 | 93.16 | 53.29 | 52.36 |

The candidate cuts smaller-tree all-results CPU about 6.3% and 5.8% versus B.
Voluntary-switch medians fall from 6,038 to 4,924 and 5,888 to 4,817. But its CPU
is still about 28% and 30% above production, and smaller-tree elapsed medians
regress about 41% and 55%. Quiet balanced elapsed regresses about 17% and 19%.
Both skew gains survive, but the repeated 5% control-regression limit fails.
The candidate is rejected, not promoted.

A second focused profile, `profile-icp4kzdu`, independently measures old B and the
candidate together. Quiet CPU is 128.93 versus 121.65 ms, and instructions are
63.640 versus 60.034 million. Yet traced futex counts remain 12,459 and 12,537,
versus production's 115. The reduction does not remove the broad contention
pattern. Do not claim that lower native context switches guaranteed fewer traced
futex calls. Persistent frame operations and stop polling still share the
scheduler mutex, including reclamation paths; their exact native cost remains
unattributed.

## Qualification and decision limits

Read-only review found no material runtime defect in the targeted change. It did
identify missing private-path evidence: active persistent counters exclude private
builders; additional tests would need to establish scratch exclusivity, empty
retirement, arena held/cached cleanup, promotion failures, and parallel cutoff/
guard cases. These are gaps, not demonstrated leaks. They were not hidden by a
full-qualification claim after a failed performance gate.

No full sanitizer/platform/coverage/semantics/fuzz/mutation qualification or
complete live-listing-bound proof is claimed for this rejected candidate. Existing
floors and required retained-code checks were not weakened. All 111 production
inputs captured before the experiment remain unchanged, including the 100 inputs
of the retained qualification. No commit, push, install, dependency edit,
publication, cache drop, affinity change, or security-setting change occurred.

The next [disposal-only experiment](FRONTIER_RECLAIM.md) shortens the persistent
reclamation critical section without changing polling or traversal policy.
Smaller all-results timings approach production, but quiet-small medians still
miss the unchanged 5% gate. Neither reduction is promoted.

## Retained evidence

[Portable index](../bench/results/frontier-cost.json),
[full compressed data](../bench/results/frontier-cost-data.json.gz), and
[summary CSV](../bench/results/frontier-cost-summary.csv) retain both matrix runs,
both focused profiles, raw Callgrind/syscall records, individual native samples,
argv/order, binary/source hashes, the failing-before log, passing-after log,
source versions, and replay/audit scripts. The focused runs add 144 native timings
and 24 validated instrumented outputs, separate from the 3,200 matrix timings.

The audit checks exact fynd hashes, counts/lengths, distributions, JSONL equality,
orders, CPU arithmetic against independent GNU time, tool/source identities,
fixture cleanup, and unchanged production inputs. A coherently doubled CPU record
fails the independent timer check. fd's raw bytes passed multiset validation on
each invocation, but reused scratch bytes cannot later be reconstructed from
scalar hashes. Profile and matrix measurements are not pooled.

All heavy work ran sequentially under actual 3 GiB/no-swap/nice-15/core-0 limits,
with at least 5 GiB free-space checks. Host load, cache residency, and scheduling
were uncontrolled. No cold-cache, slow-storage, total RSS-cap, native hardware
perf-attribution, or universal fd-parity claim is made. Only newly owned fixtures
were cleaned; prior experiments and unrelated files remain.

Local replay requires the retained binaries, source snapshots, and pinned paths:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/frontier-cost/experiment.py --offset 0'
```

This is not a fresh-checkout installer. Do not ship this candidate.
The measured partial improvement does not justify
its remaining regressions. Any further change should isolate a remaining
contended path first, not presume that another architectural layer is needed.
