# Reclamation outside the scheduler mutex

Moving persistent payload disposal outside the scheduler mutex substantially
reduces the continuation prototype's overhead. Both skew gains survive, and
smaller-tree all-results timings approach production. Quiet smaller-tree medians
still miss the unchanged 5% gate, so the candidate remains isolated.

## Probe before changing code

The preceding [private-leaf reduction](FRONTIER_COST.md) still showed high mutex
activity. The next hypothesis was specific: zero-reference frame disposal calls
allocator cleanup while holding the same mutex used for scheduling and stop polls.

A diagnostic build timed those disposal spans on small and balanced all/no-match
queries. Twelve independently validated outputs each exercised 1,554 disposals.
Aggregate thread CPU inside the spans ranged about 5.7 to 7.7 ms, wall time about
6.7 to 9.0 ms. Individual maximum spans ranged about 16 to 276 microseconds.
These spans include clock overhead and perturb lock holding and scheduling.
They are not native CPU fractions or proof of the whole regression's cause.

The alternatives remained live: pure reference/control costs or stop-poll and
fragment-handoff effects could still dominate after disposal moved. No new
scheduler, mutex, worker, staging capacity, or polling policy was introduced.

## The disposal-only change

Under the mutex, a zero-reference frame relinquishes its parent reference and
becomes a caller-owned retired control. The active counter decreases and the
retired counter increases. Its payload is still allocated; the control is not
on the reusable free list. Cascading ancestor drops retain the entire retired
chain even when they stop at a still-referenced ancestor.

After unlocking, the same worker synchronously releases listing and arena
payloads, then publishes the empty controls with one locked list splice. It
finishes this before more traversal or DONE. There is no background collector,
global garbage queue, hidden parent job, or early refund of unread output.
Retired controls remain charged to the existing cap. Their job retains its token.

Quiescent model drops accumulate chains in the task until teardown. Final cleanup
joins all workers, detaches all remaining chains under the mutex, disposes them
outside it, then checks zero active/cursor/retired ownership before destroying
primitives or the pool arena. Frame reuse cannot precede physical payload release.
Active, retired, and nonempty private builders all matter to physical listing
accounting; the active counter alone is insufficient.

## Sensitive verification

A strict-compiled test failed behaviorally before the fix with status 134 when
payload release occurred in an explicitly mutex-held model drop. The strengthened
test wraps actual mutex operations, including finish, and pauses each of three
ancestor disposals. A helper thread acquires the scheduler mutex during that
pause and verifies zero references, retained job accounting, live payloads, and
absence from the reusable control list. END checks both arena held and cached
bytes are zero; final checks find all controls empty and reusable.

The initial sensor was strengthened before the green run. A compiled behavioral
control moving disposal back under the mutex fails the strengthened assertion
before its barrier. No timeout or compilation failure
counts as rejection. Eight C executables, 508 CLI checks, 140 ignore checks,
and targeted Dependable C fast checks pass. Existing tests were not weakened.

Read-only implementation review found no actionable correctness bug. Remaining
gaps include a live native-pop disposal racing cancellation/reuse, and a complete
peak listing/control-bound proof including overlapping retired/private states.
The post-join barrier is not coverage of every runtime schedule.

## Two matched native runs

Production, the preceding private-leaf candidate, and the new disposal candidate
run in the same fixture groups. There are 16 cases, ten configurations, and ten
rotated repetitions per run. Both runs passed 320 prechecks, 3,200 timed-output
validations, and 320 separate warmups combined. Every fynd output matched
independent manifest-derived DFS bytes; fd passed multiset validation.

Median j8 elapsed milliseconds, file sink:

| Workload | Run 1 production | Run 1 previous | Run 1 disposal | Run 2 production | Run 2 previous | Run 2 disposal |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Smaller tree, all | 18.15 | 24.24 | 18.79 | 19.26 | 26.30 | 19.94 |
| Smaller tree, quiet | 17.28 | 23.94 | 18.51 | 17.66 | 25.30 | 18.70 |
| Balanced, all | 44.33 | 41.43 | 38.77 | 51.89 | 44.31 | 39.85 |
| Balanced, quiet | 30.52 | 34.70 | 30.98 | 32.15 | 36.33 | 31.38 |
| Dominant branch, quiet | 88.42 | 41.28 | 38.59 | 88.72 | 42.36 | 39.81 |
| Extra-level skew, quiet | 88.06 | 47.76 | 46.66 | 87.71 | 53.59 | 48.18 |

Smaller-tree all-results medians are about 3.5% slower than production in both
runs. Quiet medians are about 7.1% and 5.9% slower, narrowly exceeding the declared
5% repeated-regression limit. Both skew shapes still exceed the required 15%
quiet gain in both runs. The gate is not relaxed to ship the improvement.

Paired quiet-small ratios have medians 1.065 and 1.058, with exploratory nominal
95% bootstrap intervals 1.017 to 1.105 and 1.001 to 1.161. The intervals include
5%, and individual samples vary. Rejection uses the predeclared repeated median
gate, not a claim of a universal or statistically certain 5% penalty. Ratios of
independent medians differ from medians of paired ratios.

### CPU and waits

Smaller-tree all-results CPU medians fall from 111.38 to 102.19 ms and 117.49 to
104.62 ms versus the previous candidate. Production is 87.37 and 92.58 ms.
Voluntary-switch medians fall from 4,923 to 1,452 and 4,912 to 1,442, about 70%.
Quiet balanced switches fall from 4,147 to 1,790 and 4,170 to 1,762.

This supports the tested critical-section change, not an exclusive attribution
of every CPU or elapsed difference. More CPU remains than production, especially
on smaller and quiet trees. Serial logic is unchanged, but serial elapsed time
also varies with host activity and file output. No universal speedup is claimed.

## Decision and evidence

Do not promote this candidate. It is the closest measured continuation variant
so far on all-results controls, but it still fails the quiet-small gate and has
not passed full release qualification. No full sanitizer/platform/coverage,
semantics/fuzz/mutation campaign, or complete new listing-bound proof is claimed.
Required checks and the 90% per-module floor are unchanged for retained code.

All 114 pre-experiment production inputs remain unchanged, including the 100
inputs of `run.FyCw0V`. No commit, push, install, dependency edit, publication,
cache drop, CPU affinity change, or security-setting change occurred. Same-family
review found no actionable source or report defect; reviewers did not rehash
raw evidence independently.

[Portable index](../bench/results/frontier-reclaim.json),
[full compressed data](../bench/results/frontier-reclaim-data.json.gz), and
[summary CSV](../bench/results/frontier-reclaim-summary.csv) retain the probe,
matched samples, argv/order, binary/source identities, prototype versions,
red/green and compiled-control logs, designs, tests, and audit/replay scripts.
Raw runs are `build/frontier-reclaim/run-7oqxqlyg`, `run-ao12bt8m`, and diagnostic
`probe-fxriahzf`. Native and instrumented results remain separate.

The audit checks distributions, JSONL equality, counts/lengths/exact fynd hashes,
orders, tool/source hashes, cleanup, CPU arithmetic and independent GNU time.
A coherently doubled CPU record fails the independent timer. fd's raw bytes
passed multiset validation before scratch reuse; scalar hashes cannot reconstruct
those bytes afterward.

All heavy configurations ran sequentially under actual 3 GiB/no-swap/nice-15/
core-0 limits with a 5 GiB disk preflight. Only owned fixtures were cleaned.
Host load was uncontrolled, about 1.12 to 3.01 and 3.01 to 4.57 across the native
runs. No controlled-cold/full-residency, slow-storage, total RSS-cap, native perf
attribution, or universal fd-parity claim is made.

Local replay requires retained binaries and pin paths:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/frontier-reclaim/experiment.py --offset 0'
```

This is not an installer or an automatic production promotion. Further changes
must preserve this evidence and independently pass the same controls.
The subsequent [one-poll subtraction](FRONTIER_POLL.md) saves a redundant check,
but still fails smaller-tree native controls. Neither change is promoted.
