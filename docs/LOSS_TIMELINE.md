# Loss-case task timelines

This measurement separates two scheduling limits. It changes no application
code, default, dependency, or output budget. No auto mode or optimization
candidate is implemented.

## Package refill stalls are observed

At j8, the package all-file query spends a median 71.34% of its traced scheduler
window in the full-queue refill-starvation condition. The zero-output query
spends 79.68%. The condition requires all of the following at once:

- A further root entry is waiting to be offered.
- All N outstanding-task tokens are occupied.
- The coordinator is waiting on the FIFO head.
- No task is QUEUED, fewer than N tasks are RUNNING, and at least one is DONE.

DONE/unread successors remain correctly charged. They cannot retire past the
FIFO head, so the coordinator cannot submit the waiting entry. This is observed
queue state, not a guess based on sorted filenames. Task paths and actual
enqueue order are retained.

The mean DONE population is a median 5.86 of eight slots for package all-file
and 5.89 for zero-output. At j32 it grows to 26.51 and 26.44 slots respectively.
`packages/botocore` is the longest task in all 36 sampled package traces.
It is a recurring blocking head, followed by other large package tasks.

These percentages use each instrumented trace's first-to-last-event window,
then take the median of nine ratios. They are NOT native runtime cost shares,
CPU-idleness measurements, removable-time estimates, or predicted speedups.
RUNNING includes execution, I/O, preemption, and output waits.

## Skew is a different limit

Only eight whole-subtree jobs exist. `skew-branch/a` accounts for almost the
entire tree and is the longest task in all 36 sampled skew traces. Its median
start-to-DONE interval covers 99.86-99.95% of the traced window across the four
query/thread groups. Mean RUNNING task population stays near one.

There are no refill-starvation intervals in these skew traces: there is no
ninth root task waiting for a token. Completed-task bypass alone cannot provide
more work inside `a`. Workers currently recurse privately and cannot share
that subtree's descendants.

## Producer credits are not exhausted

All 80 retained traces, including eight probe prechecks, record zero producer
credit-wait entries. The package zero-output case reproduces refill starvation
without stdout payload. The skew zero-output case reproduces the indivisible
large task. Increasing the N MiB payload budget is not supported by these results.

This does not rule out stdio costs, lock costs, or output effects under other
workloads. It distinguishes task-token/refill limits from this producer-credit
wait mechanism on the measured cases.

## Native measurements remain separate

The same two historical metadata manifests are materialized into new owned
fixtures. Native measurements use the exact qualified streaming binary, not
the probe. Nine repetitions cover fynd j1/j8/j32 and fd-auto. All values below
are elapsed milliseconds; they are not pooled with the earlier full benchmark.
Absolute timings differ from that earlier run, and no cause is assigned to
that difference.

| lane | case | configuration | median_ms |
| --- | --- | --- | --- |
| NATIVE | packages-all-file | fynd-j1 | 35.39 |
| NATIVE | packages-all-file | fynd-j8 | 35.17 |
| NATIVE | packages-all-file | fynd-j32 | 32.87 |
| NATIVE | packages-all-file | fd-auto | 23.38 |
| NATIVE | packages-none-file | fynd-j1 | 33.56 |
| NATIVE | packages-none-file | fynd-j8 | 35.50 |
| NATIVE | packages-none-file | fynd-j32 | 31.55 |
| NATIVE | packages-none-file | fd-auto | 18.62 |
| NATIVE | skew-branch-all-file | fynd-j1 | 121.64 |
| NATIVE | skew-branch-all-file | fynd-j8 | 108.27 |
| NATIVE | skew-branch-all-file | fynd-j32 | 122.32 |
| NATIVE | skew-branch-all-file | fd-auto | 52.20 |
| NATIVE | skew-branch-none-file | fynd-j1 | 110.75 |
| NATIVE | skew-branch-none-file | fynd-j8 | 105.42 |
| NATIVE | skew-branch-none-file | fynd-j32 | 109.99 |
| NATIVE | skew-branch-none-file | fd-auto | 39.57 |

These fresh native medians still lose to fd-auto, including package zero-output.
There is no native scheduling change and no performance improvement claim.

## Perturbed scheduler-window observations

Each row contains medians of nine individual traces. Thread settings and queries
remain separate. Percent fields use the traced window only. Task populations
are wall-time integrals of scheduler state, not per-thread CPU counters.
`credit_wait_events` counts entries into the producer-space wait path.

| lane | case | configuration | mean_running_tasks | mean_done_tasks | refill_starvation_percent | longest_task_percent | credit_wait_events |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PERTURBED | packages-all-file | fynd-j8 | 1.73 | 5.86 | 71.34 | 29.14 | 0.00 |
| PERTURBED | packages-all-file | fynd-j32 | 3.86 | 26.51 | 62.15 | 30.30 | 0.00 |
| PERTURBED | packages-none-file | fynd-j8 | 1.83 | 5.89 | 79.68 | 26.49 | 0.00 |
| PERTURBED | packages-none-file | fynd-j32 | 3.83 | 26.44 | 60.16 | 33.23 | 0.00 |
| PERTURBED | skew-branch-all-file | fynd-j8 | 1.00 | 4.99 | 0.00 | 99.87 | 0.00 |
| PERTURBED | skew-branch-all-file | fynd-j32 | 1.01 | 4.99 | 0.00 | 99.85 | 0.00 |
| PERTURBED | skew-branch-none-file | fynd-j8 | 1.01 | 4.99 | 0.00 | 99.94 | 0.00 |
| PERTURBED | skew-branch-none-file | fynd-j32 | 1.01 | 4.99 | 0.00 | 99.93 | 0.00 |

## Probe and evidence checks

Run `run-fubrgfe1` retains 16 native prechecks, eight probe prechecks, 144 native
samples, 36 validated native warmups, and 72 sampled traces. All 276 finder
outputs validate against independent manifest references. All 80 raw traces
pass lifecycle, slot-generation, FIFO-head, chronology, and task-count checks.
Native CPU totals are cross-checked against unchanged GNU-time records.

A private copy of the selected qualified source adds hooks under the existing
PoolMu. Hooks copy paths only while live, never inspect producer-private FILL
bytes, and never allocate, print, wait, or acquire a new lock. Trace output occurs
after the finder returns and workers have joined. Exact additive source patches
are retained. Static bounds are 32 slots, 32,768 events, 4,096 jobs, and 1,024
bytes per copied path. Overflow invalidates the trace rather than enlarging it.

The strict C89 optimized build passes. Small normal and Clang ASan/UBSan probe
checks preserve actual output and raw high-byte/LF paths. An eight-event overflow
control produces correct finder output but marks the trace invalid; the checker
rejects it for that reason. In-memory controls reject wrong lifecycle order,
state counts, timestamps, head identities, and coherently forged CPU counters.
These are diagnostic controls, not release mutation kills or a new qualification.

All heavy work is sequential in a 3 GiB, zero-swap, nice-15 scope with core limits
zero, including the outer relay. At least 5 GiB disk is required. Scope memory-max,
OOM, and swap counters remain zero, and the owned fixtures are absent after cleanup.
No limits, host security, affinity, cache policy, or dependency versions are changed.
All 102 selected qualified working inputs and archived dependency contents match
before and after. c-utils remains pinned to
`eafa795805fa997e2db91f9182c4e0f86fa32fb4`.

The probe scans task states and timestamps transitions while holding PoolMu,
so it perturbs the scheduling being observed. Probes follow the entire native
lane. Instrumented/native wall-time ratios span about 0.40-1.53 across the
non-adjacent samples; they are not an overhead correction. Nine rotations are
not fully position-balanced. These are shared-host, heated metadata-layout
measurements, not controlled-cold or live-package-content tests. No first-byte
measurement or native CPU-function attribution is made.

## Implication for the next change

Auto-selecting more threads does not remove either mechanism. Package work
needs progress past completed FIFO successors without releasing unread bytes
or splitting records. Skew needs finer work inside an assigned subtree.
Those are separate design and verification problems, not one thread-count knob.

A future candidate must keep N outstanding tasks, including DONE/unread work,
and N MiB aggregate payload. It must preserve contiguous records, root groups,
and join-before-borrowed-storage-release. No such candidate is included here.
The previously rejected continuation variants remain rejected.

## Retained evidence

- [Index](../bench/results/loss-timeline.json)
- [Compressed data](../bench/results/loss-timeline-data.json.gz)
- [Separate native and perturbed CSV rows](../bench/results/loss-timeline-summary.csv)
- Local run, traces, source patches, and logs: `build/loss-timeline/`
- Selected qualification: `build/streaming/qualification/build/verification/run.vbgYhn`

Successful raw stdout captures are reused. Canonical hashes/counts and journals
attest runtime validation, not retrospective rehashing of every raw stdout.
The archive retains raw task traces, manifests, references, source copies, and
binary hash receipts. It is not a cryptographic binary/source attestation.

Replay analysis without new fixtures, compilation, or finder runs:

```sh
cd /home/travers/projects/fynd
ulimit -c 0
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/loss-timeline/final_check.py --live'
```

Without `--live`, the checker validates the packed evidence using the current
hash-bound trusted scripts, without original run directories or binaries.
Nothing is committed, pushed, installed, or published.
