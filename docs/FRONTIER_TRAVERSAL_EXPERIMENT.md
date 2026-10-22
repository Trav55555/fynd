# Rejected continuation prototype

The continuation design redistributes the deeper heavy branch, but its overhead
fails the retention gate. It is not promoted. Production remains the qualified
shared-credit walker from `run.FyCw0V`.

## Paired native results

Two independent runs each compared baseline/prototype j1/j4/j8/j16 and fd auto
across 16 workload/sink cases, with nine rotated repetitions. Totals are 288
prechecks, 2,592 timed outputs, and 288 separate validated warmups. Every fynd
output matched independent manifest-derived DFS bytes; fd passed file-multiset
validation, not ordering parity.

Median elapsed milliseconds at j8, file output unless marked pipe:

| Workload | Run 1 baseline | Run 1 prototype | Run 2 baseline | Run 2 prototype |
| --- | ---: | ---: | ---: | ---: |
| Dominant branch, all | 93.95 | 47.40 | 124.98 | 62.48 |
| Dominant branch, no matches | 90.11 | 43.84 | 91.65 | 44.47 |
| Extra-level skew, all | 93.10 | 60.31 | 102.95 | 79.20 |
| Extra-level skew, no matches | 91.42 | 51.70 | 93.65 | 56.23 |
| Balanced, all | 49.06 | 43.90 | 51.50 | 53.72 |
| Balanced, no matches | 32.83 | 37.03 | 32.64 | 39.85 |
| Smaller branching tree, all | 18.61 | 27.04 | 24.86 | 40.36 |
| Smaller branching tree, no matches | 17.97 | 26.58 | 17.79 | 28.89 |
| Dominant branch, all, pipe | 93.10 | 47.47 | 94.03 | 51.83 |
| Balanced, all, pipe | 49.59 | 43.63 | 48.01 | 51.34 |

Both skew shapes exceed the required 15% quiet j8 gain in both runs. The extra
level that defeated the [counted-parent prototype](SKEW_TRAVERSAL_EXPERIMENT.md)
now improves about 43% and 40%. But smaller-tree all-results medians regress
about 45% and 62%, far beyond the declared 5% repeated-regression limit. Quiet
balanced medians regress about 13% and 22%. These costs reject the prototype.

CPU corroborates the control cost despite noisy elapsed times. Smaller-tree
all-results CPU medians rise from 89.62 to 121.59 ms and 92.03 to 124.90 ms,
roughly 36% in each run. Quiet balanced CPU rises from 161.19 to 192.35 ms and
166.73 to 197.62 ms. Extra-level quiet-skew CPU rises more modestly, 92.12 to
97.65 ms and 93.41 to 102.29 ms, while elapsed time falls.

The new frame/cursor allocation, reference ownership, and synchronization paths
are plausible targets for profiling. These measurements do not attribute a
specific fraction of CPU to any of them. Do not infer that removing one lock
would fix the regression. Serial walker logic and CLI remained unchanged; serial CPU
measurements did not show a similar broad increase.

## Architecture and the smaller scope chosen

The earlier nested design waited for children while retaining parent task tokens.
This design has no parent-worker waits. Every queued, running, DONE/unread, or
currently delivered fragment still occupies one of N slots. Fixed root-group
metadata merely records which contiguous fragments belong to one original
subtree; it owns neither a worker nor output storage.

Workers walk stable Frames and Cursor ranges. A Frame owns its listing, path,
ignore state, and a strong parent reference. A Cursor owns one Frame reference,
a half-open entry range, and a unique outer-remainder chain. Workers edit only
their own cursor while RUNNING. All reference transfers and control allocation
or reclamation occur under the scheduler mutex. Frame/Cursor controls are stable,
reused, and capped at `128 * (N + 1)` each. Listing widths and arena storage remain
additional to the N MiB output payload.

At a completed useful directory boundary, the current head can finish its prefix
and yield. Main first delivers every prefix byte, flush marker, and status while
retaining the token. Only then does it split the unvisited range. Left ends at
the cut with no outer continuation; right inherits the original outer remainder
and enters immediately after left. This preserves DFS order without copying or
replaying the outer work. Main consumes the entire root group before advancing
its original coordinator entry.

The initial B sketch proposed execution quanta and resumable byte/diagnostic
emission. The prototype removes both: it yields only at completed fork boundaries
and reuses the existing blocking byte transport. With N workers and N fragments,
a queued head leaves an available worker; a running head has its own reserved
credit and Main can drain it. No worker waits on descendant completion.
The N MiB payload and reserved/shared 256 KiB batch policy remain unchanged.

This does not guarantee unlimited subdivision. Future DONE fragments still hold
tokens, so enough later intervals can exhaust split capacity. The design changes
which work can use the available slots, not the global bound. No unconsumed output
or diagnostic is relabeled as uncharged frontier metadata.

## Verification and limits

The isolated prototype passed strict C89 native builds, six C executables,
508 CLI checks, 140 ignore checks, and targeted Dependable C fast checks.
The old tests remained intact. A new deterministic fixture holds five future
root jobs DONE, consumes two successive prefixes, and proves two workers enter
heavy grandchildren concurrently at eight pending fragments, without parent
workers waiting.

The new tests also check unique outer-range transfer, shared-parent references,
exact raw-byte and ignore-aware output, short FILE-write cancellation, joined
cleanup before scope release, and zero active frames/cursors after cleanup.
Quiescent model tests inject split allocation failures and check transactional
rollback and reference counts when left/right continuations release separately.
Read-only source review found no blocking defect. Reviewers used the same model
family; no independent reviewer rehash or different-family review is claimed.

This is not full release qualification. Full sanitizer, platform, coverage,
semantics, fuzz, and mutation checks were not run after the failed native gate.
The tighter live-listing bound also lacks complete new-reference-graph evidence.
The control cap alone is not proof of that bound. No coverage floor, required
profile, error policy, or production guarantee was weakened to retain this code.

All 108 production inputs captured before this experiment remain unchanged,
including the 100 inputs of the retained qualification. No promotion, dependency
edit, thread-default change, commit, push, install, publication, cache drop, CPU
affinity change, or security-setting change occurred.

## Evidence and replay

[Portable index](../bench/results/frontier-traversal.json),
[full compressed data](../bench/results/frontier-traversal-data.json.gz), and
[summary CSV](../bench/results/frontier-traversal-summary.csv) retain both runs,
individual measurements, exact argv/order, binary/source hashes, design/interface
contracts, prototype source/tests, and replay/audit scripts. Raw runs are
`build/frontier-traversal/run-9urj4u80` and `run-zv4d841a`.

The audit rechecks JSONL equality, output counts/lengths/exact fynd hashes,
distributions, order, CPU arithmetic against independent GNU time, identities,
fixture cleanup, and unchanged production inputs. A coherently doubled CPU
record fails the independent timer check. Raw fd bytes were multiset-validated
per invocation, but reused scratch bytes cannot be revalidated later from scalar
hashes alone. The two runs remain separate rather than pooled.

Configurations run sequentially within actual 3 GiB/no-swap/nice-15/core-0 limits.
The driver refuses execution below 5 GiB free space. Only its new owned fixtures
are cleaned; existing evidence and unrelated files remain. Host load was not
controlled, roughly 2.93 to 7.55 and then 7.55 to 6.51. There is no controlled
cold-cache, slow-storage, total RSS-cap, native perf-attribution, or universal
fd-parity claim.

Local replay requires retained baseline/prototype binaries and pin paths:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/frontier-traversal/experiment.py --offset 0'
```

Do not replace production with this prototype. The subsequent
[management-cost investigation](FRONTIER_COST.md) measures that overhead and tests
one task-private leaf reduction. It improves some costs but still fails the
retention gate; neither continuation variant is promoted.
