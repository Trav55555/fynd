# Coarse ordered traversal

> **Historical.** `src/coarse.inc` and its tests were removed when the
> [shared-stack scheduler](SCHEDULER.md) replaced this design.

Historical ordered walker. The [current streaming walker](STREAMING.md) removes
complete listings and sorting while retaining bounded FIFO whole-task transport.
Its output within each root is unspecified, and late read errors keep earlier
results. The measurements below remain evidence for their original versions.

This records the initial sibling-subtree implementation with dedicated output
mailboxes. Serial remains the default, and a dominant subtree remains a weakness.
The later [performance pass](PERFORMANCE_PASS.md) retains the walker and job bound
but replaces dedicated mailboxes with shared output credits.

For the broader follow-up, see [the comprehensive benchmark](COMPREHENSIVE_BENCHMARK.md).
It finds remaining gaps on larger output-rich trees, recursive skew, package
layouts, and heavy ignore rules. The narrower results below remain historical.

## Qualified native comparison

The final run, `build/coarse-traversal/run-pzi1sy79`, compared the exact qualified
binary with the previous qualified implementation and installed fd 10.3.0.
Nine rotated repetitions produced 126 prechecks and 1,134 timed outputs. Every
output matched an independent creation manifest. Every fynd output also matched
the previous serial binary byte for byte; fd was checked for file multiplicity,
not traversal order.

Median elapsed milliseconds, all selected regular files:

| Tree | Previous j1 | Previous j8 | New j1 | New j4 | New j8 | fd auto |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Flat, 100k files | 47.480 | 47.741 | 43.658 | 44.142 | 45.228 | 44.337 |
| Deep, 96 levels | 5.210 | 7.135 | 5.191 | 5.405 | 5.694 | 8.920 |
| Mixed | 29.312 | 28.137 | 29.003 | 13.136 | 9.475 | 13.958 |
| Many directories | 72.081 | 91.533 | 71.250 | 29.928 | 20.528 | 26.201 |
| Single-child prefix before a fork | 18.712 | 21.049 | 19.535 | 9.628 | 7.973 | 11.684 |
| One dominant subtree | 17.162 | 17.764 | 16.666 | 18.709 | 18.656 | 10.064 |
| Output-heavy, 9.84 MB | 50.480 | 23.638 | 49.137 | 18.850 | 15.923 | 23.582 |

The many-directory improvement is about 3.5 times versus previous serial and
4.5 times versus previous j8. New j8 used about 4.67 CPU cores on average there,
versus 1.82 for previous j8. It spent 96.1 CPU ms versus 164.0, including the small
GNU-time launcher cost. These estimates use OS CPU accounting, which excludes
I/O wait and descheduling; earlier collector occupancy included both.

No-match queries also improved: many-directory elapsed time fell from 85.5 ms
for previous serial to 20.2 ms at new j8. The gain does not depend on printing
many records. Output-heavy subtrees each exceed the 1 MiB mailbox, so those
measurements also exercise real backpressure.

## Why this shape

The old workers collected and sorted individual listings. Traversal, paths,
ignore decisions, selection, and output preparation remained on the coordinator.
Sending more tiny listings to workers had previously made latency worse.

The new workers run the same recursive walker on sibling subtrees. The
coordinator follows single-child prefixes until a useful fork, then drains jobs
in sibling order. Workers never recursively submit work. Each task has one
bounded byte mailbox, and the coordinator alone writes caller streams.

The private listing no longer carries prefetch pointers or scheduling tokens.
This also changes serial overhead and per-entry storage; the serial differences
above are not isolated to one cause. Selection, raw output, root order and
duplicates, ignore policy, depth limits, and failure handling remain unchanged.

Two designs were compared before implementation. The selected sibling-window
design scored 15/18; resumable continuations scored 12/18 in the same-model review.
Continuations could rebalance skewed work, but required more state and ownership
machinery. No different-model-family review is claimed.

## Costs and limits

- Whole-subtree jobs cannot repartition their heavy branch. On the skew fixture,
  new j8 was slower than serial and fd remained much faster.
- Output order can stall later producers behind the earliest subtree. A 256 KiB
  mailbox ablation raised many-directory j8 latency from 20.5 to 27.5 ms here.
  The 1 MiB default was retained; it is not claimed optimal for every workload.
- Each running worker can retain its own ancestor chain. The former allowance
  of serial ancestors plus N extra listing widths no longer applies. The live
  listing bound is `max(128, 1+127*N)`, plus N mailboxes and other actor storage.
  See [the full memory contract](IGNORE_THREADS.md#jobs-output-and-memory).
- Many-directory peak RSS was about 3.3 MiB at new j8, versus 1.7 MiB for previous
  serial and 11.4 MiB for fd auto. On the output-heavy fixture, new j8 used 19.6 MiB
  versus 14.5 MiB for previous j8. These observations are not memory ceilings.
- The fixtures use warm NVMe ext4, empty files, hidden entries, and disabled
  ignores. This is not an ignore-engine, real-repository, cold-cache, slow-storage,
  or universal fd-parity result. fd auto uses its own ordering and thread policy.
- Background host load was uncontrolled, about 5.76 to 7.70 during the final run.
  Configurations and workloads rotated; no timing-ratio acceptance gate was used.

Startup, GNU time, and output-file I/O are included in elapsed time. CPU uses
OS child-resource deltas around each sequential launcher/application tree.
Independent component medians need not add up to the total median.

## Qualification and retained evidence

Full acceptance passed at
`build/coarse-traversal/prototype/build/verification/run.XXAMxe`, exit 0.
All 6,656 CLI comparisons, four C executables, six benchmark tests, and required
deep profiles passed. Profiles include TSan, MSan, ASan/UBSan, analyzer,
optimized builds, i386, WASI, Valgrind, four bounded semantics cases, and four
fuzz targets.
All 19 compiled behavioral controls were rejected; no timeout counted as a kill.
Per-module line coverage remained above the unchanged 90% floor.

Deterministic fixtures cover real directory overlap, full later mailboxes,
exact-capacity final tails without consumer acknowledgement, and DONE-inclusive
job limits. They also cover stream/channel transitions, long raw paths and
escaped diagnostics, startup/allocation failures, and ordinary versus terminal
worker errors. A nested
output-failure fixture holds workers borrowing parent rules and asserts they
join before coordinator CuTemp scopes end. Optional authority, regions, and
CBMC checks retained their explicit skips. See [verification](VERIFICATION.md).

All 61 captured inputs matched after promotion. No dependency, thread default,
commit, push, installation, or publication changed. The immutable c-utils pin
remains `eafa795805fa997e2db91f9182c4e0f86fa32fb4`.

[Portable measurements and source snapshots](../bench/results/coarse-traversal.json)
retain tool versions/hashes, samples, manifests, source hashes, controls,
coverage, and replay source/driver text. The earlier exploratory run is retained
at `build/coarse-traversal/run-z4nvrfyc`; the table uses only the final qualified run.

Local replay uses `build/coarse-traversal/experiment.py` under the usual 3 GiB,
no-swap, nice 15, core 0 scope. It needs the retained acceptance receipts. The JSON
includes replay inputs, but receipt-specific binary paths must be rebound for a
fresh workspace and the pinned dependency supplied separately. This is not a
one-command fresh-checkout benchmark or an installer.
