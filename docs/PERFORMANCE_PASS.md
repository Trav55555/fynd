# Shared output credits and ignore rejection

Historical acceptance and measurements. [Current streaming](STREAMING.md) keeps
the shared-credit and ignore-filter components but replaces sorted traversal
and whole-directory storage. Ordering and source-identity claims below apply
to the recorded versions, not the current working tree.

Retained two local optimizations: shared bounded output batches and a conservative
ignore-rule rejection filter. Serial remains the default. Successful output still
follows unsigned-byte sorted depth-first search preorder, including root order
and duplicates.
The immutable c-utils pin and public `fynd_main` interface did not change.

## Paired native results

The selected 256 KiB implementation is the exact qualified binary from
`build/performance-pass/combined/build/verification/run.FyCw0V`.
The comparison uses the previous qualified coarse walker from `run.XXAMxe`,
not the older listing-only thread implementation.

Final run `build/performance-pass/run-3n4wm0_5` passed 363 configuration prechecks,
3,267 timed-output validations, and 297 separate validated warmups. There are
33 workload/sink cases, 11 configurations, and nine rotated repetitions.
All fynd outputs matched independent manifest-derived depth-first bytes. fd outputs
matched the selected-file multiset, not fynd's ordering.

Median elapsed milliseconds, file sink unless marked pipe:

| Workload | Before j1 | After j1 | Before j8 | After j8 | fd auto |
| --- | ---: | ---: | ---: | ---: | ---: |
| Smaller branching, 37,324 files | 89.546 | 91.547 | 22.635 | 27.886 | 30.932 |
| Many directories, 223,948 files | 493.182 | 518.448 | 287.520 | 160.384 | 149.544 |
| Same many-directory tree, no matches | 453.630 | 458.041 | 127.341 | 117.965 | 97.846 |
| Many directories, pipe | 462.866 | 468.362 | 229.670 | 117.052 | 108.378 |
| Balanced, 298,592 files | 178.138 | 210.015 | 123.869 | 58.980 | 66.822 |
| Single-child prefix, 18,660 files | 19.718 | 19.833 | 7.213 | 6.526 | 11.615 |
| Dominant branch, 18,674 files | 18.790 | 18.268 | 17.485 | 17.757 | 10.442 |
| Flat, 100,000 files | 43.660 | 44.081 | 45.273 | 43.859 | 42.953 |
| Shared 200-byte prefix, 20,000 files | 29.721 | 29.649 | 29.278 | 30.257 | 24.116 |
| Long output names, 48,000 files | 60.805 | 60.113 | 19.467 | 17.299 | 23.575 |
| 64 extra literal-leading miss rules | 26.741 | 7.365 | 8.693 | 3.858 | 8.088 |
| 512 extra literal-leading miss rules | 175.094 | 20.336 | 67.835 | 8.138 | 15.096 |
| 128 wildcard-leading miss rules | 60.181 | 62.881 | 17.751 | 18.121 | 34.279 |
| 128 colliding-first-byte miss rules | 50.063 | 49.674 | 14.952 | 14.154 | 9.068 |
| Empty | 1.317 | 1.272 | 1.612 | 1.546 | 3.791 |

This improves the targeted large-output and literal-leading-rule cases, not every
search. The smaller branching all-results case regressed about 23% at j8.
Dominant-branch work still cannot repartition itself. Wildcard-leading and
colliding-first-byte controls do not reproduce the large ignore speedup.

A later [focused regression investigation](REGRESSION_INVESTIGATION.md) did not
reproduce the large smaller-tree or balanced-serial regressions in two matched
runs. No code changed. That follow-up does not replace these original measurements
or establish their exact cause.

Elapsed time is noisy. The many-directory j8 p10/p90 changed from 228.9/292.4 ms
to 126.2/308.6 ms. Its pipe p10/p90 changed from 208.5/272.5 ms to 100.7/129.4 ms.
Load average fell from 12.93 to 4.59 during the final run. Background work was not
controlled, and no ratio is an acceptance gate. The balanced serial wall median
rose 178.1 to 210.0 ms, while CPU medians were 148.9 and 152.0 ms. These
measurements do not establish a corresponding intrinsic CPU regression.

### CPU, handoffs, and memory

The large all-results j8 case spent 475.3 CPU ms before and 523.4 after, despite
lower elapsed time. This is useful overlap, not reduced traversal CPU. The
512-rule case spent 289.7 CPU ms before and 34.1 after. Its serial CPU medians
were 173.2 and 19.9 ms. Independent GNU time CPU fields cross-check resource
accounting, within 20.835 ms in the final run including rounding and launcher cost.
A coherently doubled CPU/user/system/cores record fails that independent check.

The 64 KiB variant also passed full qualification and remains an explicit ablation.
In the final run, it had lower many-directory file and pipe medians, 147.9 and
103.3 ms, than the selected 256 KiB default. The larger batch reduces metadata
and handoffs: balanced all-results voluntary-switch medians were 393 at 64 KiB
and 183 at 256 KiB; CPU medians were 178.7 and 170.6 ms. Long-output file medians
were 19.3 and 17.3 ms. Neither batch size is universally faster. The selected
default favors fewer handoffs and controls, rather than claiming the lowest
median on every tree. The smaller-tree regression remains in this matrix.

Many-directory j8 peak RSS medians rose from 7.69 to 9.06 MiB. Long-output RSS
fell from 21.20 to 17.20 MiB. Equal staging capacity does not imply equal RSS or
allocation layout. These are observed values, not memory ceilings.

## Changes and bounds

`src/coarse.inc` now allocates a fixed typed batch array and the same N slabs of
1 MiB payload. Each slot reserves one 256 KiB batch; the remaining 3N credits
are shared. Default control count is 4N, at most 128. A task may borrow more than
the former dedicated 1 MiB, but aggregate payload capacity remains N MiB.
Queued, running, DONE/unread, and currently delivered jobs still share N tokens.

Producers fill private credits, publish per-task FIFO chunks under the mutex,
and acquire another credit only when needed. A reserved credit cannot be stolen.
Even when future jobs consume every shared credit, the head can emit through its
reserve. The coordinator pins each dequeued batch through actual FILE I/O outside
the lock. Successful delivery returns the credit and wakes all space waiters.
Final exact-capacity tails need no replacement credit or final acknowledgement.
Cancellation joins workers before releasing batches or borrowed parent frames.
The walker, sibling scheduling, and global job bound did not change.

This is not the earlier dedicated-256-KiB-mailbox experiment, which reduced total
staging capacity. Here 256 KiB is only the chunk size within unchanged N MiB.
Ancestor listings, paths, ignore data, batch metadata, allocator slack, and thread
stacks remain additional. See [the memory contract](IGNORE_THREADS.md#jobs-output-and-memory).
No total-process RSS bound is claimed.

`src/ignore.inc` rejects only impossible first-literal-byte matches. Anchored
rules compare the first byte directly. Unanchored rules lazily scan component
starts into a charged 32-byte local set, once per file verdict. Initial `*`, `?`,
or `[` falls back to the existing matcher. Escape/slash boundaries, raw high bytes,
last-rule precedence, and exact spans retain differential tests. No parser fields,
shared caches, allocations, raised work limits, or new ignore syntax were added.
Less actual work can finish where the former matcher exhausted its fixed budget;
identical tick totals or resource-failure observations are not promised.

Cancellation/replay was rejected as an alternative transport design. Discarding
and repeating a faulted attempt could change status and diagnostics. Shared
credits avoid replay and keep the existing walker and caller signatures.

## Qualification

`run.FyCw0V` exited 0. All 6,656 CLI comparisons, five C executables, 34 benchmark
unit tests, and required deep profiles passed. These include strict C89 compiler
checks, analyzer, ASan/UBSan, MSan, TSan, optimized builds, i386, WASI, Valgrind,
four bounded semantics cases, and four ten-second fuzz targets. Authority,
regions, and CBMC retain explicit optional skips. Every module exceeds the
unchanged 90% line floor:

| Module | Covered / total | Percent |
| --- | ---: | ---: |
| `src/coarse.inc` | 406 / 428 | 94.86% |
| `src/fynd.c` | 522 / 534 | 97.75% |
| `src/ignore.inc` | 462 / 477 | 96.86% |
| `src/listing.inc` | 125 / 133 | 93.98% |
| Combined | 1,515 / 1,572 | 96.37% |

All 24 compile-valid behavioral controls were rejected, without counting compile
failures or timeouts. New sensors cover reservation theft, wrong IO ownership,
failed-IO reuse, and ignore-filter correctness and charged-work behavior.
Deterministic tests conserve every credit across private fill, queued, IO, free,
and canceled states. They prove head-reservation progress, shared-credit wakeup
of a different producer, IO pinning, mixed channels, exact tails, and join-before-
parent-scope cleanup. The old thread scenarios remain at their original small
geometry. These checks are not a whole-program scheduling or filesystem proof.

All 100 captured inputs matched qualification after the eight-path promotion.
The working binary rebuilt against the qualified dependency archive passed its
five C tests, 508 CLI checks, 140 ignore checks, and 34 benchmark tests.
Another 23 independent exact-byte smoke checks passed, including j32, duplicate
roots, raw names, depth, hidden policy, and parent-rule sharing.
Read-only review found no actionable defect. Reviewers used the same model family;
no different-family review is claimed. No commit, push, install, dependency edit,
or publication occurred.

## Evidence and replay

[Portable index](../bench/results/performance-pass.json),
[full compressed data](../bench/results/performance-pass-data.json.gz), and
[summary CSV](../bench/results/performance-pass-summary.csv) retain all samples,
orders, exact argv, tool hashes, source versions, qualification and sensitivity
receipts, and replay scripts. The early screen and 64 KiB qualification run remain
separate, not pooled with final measurements. Across all three runs there are
983 prechecks, 7,687 timed outputs, and 739 validated warmups.

The driver uses the existing comprehensive runner's tested subprocess and output
validation functions. Fixtures contain empty files, hidden names, and independent
creation manifests. It validates file and captured-pipe bytes, never `/dev/null`
output-discard timing. Only new owned fixture trees are removed; historical logs
and runs remain. Reused scratch outputs mean raw fd bytes cannot be revalidated
afterward from stored scalar hashes; they passed multiset checks during each
invocation. The audit rechecks exact fynd hashes, counts/lengths, distributions,
orders, source/binary identities, JSONL equality, cleanup, and CPU accounting.

Local reproduction needs the retained baseline/variant binaries and pin archive.
The embedded driver is not a one-command fresh-checkout installer. Rebind those
receipt-specific paths for another workspace. To rerun locally:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/performance-pass/experiment.py \
  --final-receipt build/performance-pass/combined/build/verification/run.FyCw0V --repeats 9'
```

The driver refuses execution without the actual 3 GiB/no-swap/nice-15/core-0
limits. The cap includes Python, children, and charged cache. There is no
controlled cold-cache, full-residency, slow/network-storage, live-repository,
Windows, universal fd parity, or native perf-attribution claim. Host perf policy
remains unchanged. The earlier [comprehensive benchmark](COMPREHENSIVE_BENCHMARK.md)
still describes the prior binary and its broader workloads.
