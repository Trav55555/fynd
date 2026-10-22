# Historical retained large-listing memory improvement

[Qualified unordered streaming](STREAMING.md) now supersedes this implementation
and its current-at-the-time qualification. It removes directory-wide record
storage entirely. Source-identity counts below refer to the historical promotion,
not a claim that these older inputs still match the current working tree.

The gated record-arena change was retained locally at this stage. It reduces measured
million-file peak RSS from about 63 MiB to 39 MiB, roughly 38%, with modest,
workload-dependent latency gains. The earlier tiny j8 flag did not reproduce
in a focused unchanged-binary follow-up. Full required qualification then passed.
No commit, push, install, or publication occurred.
The later [latency experiment](LISTING_LATENCY.md) leaves this retained version
unchanged after rejecting one prefix-aware sort candidate.

Historical qualification: `build/listing-qualification/candidate/build/verification/run.Nano7T`.
Exact qualified executable: its `source/fynd/build/native/fynd`.
Pinned c-utils: `eafa795805fa997e2db91f9182c4e0f86fa32fb4`, archived immutably.
All 102 captured qualification inputs match the working tree after promotion.

## What changed

Only one application file changes: `src/listing.inc`. The change described in
[the original experiment](LARGE_LISTING.md) keeps name/path storage stable and
moves the entry vector to a separate arena after 1,024 entries. Small listings
keep the same allocation requests, though arena metadata initialization and
cleanup add work. No arena owner moves; checked CuVec reservation precedes a
bounded copy and control transfer. Both arena failure flags and append failure
prevent partial-listing acceptance, and both owners are released.

Sorting, unsigned-byte DFS order, CLI behavior, ignore/hidden/depth policy,
root duplicates, borrowed stream handling, N-job accounting and N MiB output
payload limits remain unchanged. Directory listing widths, arena metadata,
paths, allocator slack and stacks remain outside the output-payload bound.
No continuation prototype, replay, new thread, or runtime diagnostic flag is
included. The six application/header/CLI source inputs are byte-identical to
the measured memory-saving candidate. Qualification added tests, not another
performance patch.

## Tiny-control follow-up

The original full matrices flagged five-file all-results j8 medians by 9.10%
and 14.18%. Their paired evidence was noisy. Those measurements and their gate
flag remain in the original report and compressed data; they are not relabeled
as passing or erased.

The next test was preregistered before timing: two independent waves, 56
repetitions each, randomized block order with balanced seven-way rotations,
shuffled case order, and a duplicate-baseline label using the exact same binary
and argv. Six cases cover empty/tiny all/quiet with file and captured-pipe sinks.
Baseline/candidate j1/8/16 and baseline-check j8 run in the same groups.
No application code changed between the original matrices and this follow-up.

Primary tiny-all-file j8 ratios:

| Statistic | Wave 1 | Wave 2 |
| --- | ---: | ---: |
| Candidate / baseline medians | 0.9997 | 1.0089 |
| Median paired candidate ratio | 0.9959 | 0.9993 |
| Nominal paired 95% interval | 0.9754 to 1.0262 | 0.9773 to 1.0239 |
| Identical-baseline median ratio | 1.0022 | 1.0154 |
| Median paired identical-baseline ratio | 1.0082 | 0.9916 |

Neither candidate statistic exceeds the unchanged 5% threshold in either wave
on the primary target, and the same-binary primary control stays below it.
The frozen interpretation therefore permits advancing to qualification. All
candidate case/thread point-median ratios in these waves remain below 1.05.

The control is not perfect evidence of isolation. The identical baseline's
empty-file median ratio reaches 1.0647 in one wave, while its paired median is
1.0103. Other exploratory intervals also cross 1.05. This shows why a ratio of
independent medians and a median of paired ratios are distinct observations.
It does not prove that every prior difference was noise or that future tiny
penalties cannot occur. No statistical non-inferiority, multiple-comparison,
isolated-host or cache-residency guarantee is claimed.

Both waves pass 84 prechecks, 4,704 timed exact-output validations, and 672
separate warmups combined. Source/binary identity, raw JSONL, distributions,
position balance and exact output hashes were audited. GNU time cross-checks
are coarse at roughly 2 ms; their 30 ms tolerance cannot reliably reject
coherent doubling of a tiny CPU record. The timer negative control instead uses
a retained CPU-heavy million-file sample and rejects doubled CPU values there.
Host load is roughly 0.91 to 1.00 and 1.00 to 1.00 across these waves, not a
controlled-host experiment or a causal explanation for the older flag.

## Missing failure evidence added

Two new test files are retained with the application change:

- `tests/test_listing.c` checks size boundaries, a normal-arena memory budget,
  record migration/growth failures, record limits, and name-copy failures before
  and after migration. The late failure occurs with live healthy record storage
  and checks complete error propagation, no sort and dual-arena cleanup.
- `tests/test_listing_worker.c` drives real workers through migration/growth
  ENOMEM. It checks discarded partial children, discarded speculative future
  output, exact diagnostics, completed joins before the borrowed parent listing
  is released, and zero held/cached bytes for both owners.

The existing before-fix memory sensor still supplies a compile-valid behavioral
RED: production exceeds its fixed fixture budget after cleaning all owned files
and aborts with status 134. This is not a whole-process RSS bound. Debug arenas
explicitly exclude that normal-allocation budget because of their per-allocation
headers; their semantic/fault/lifetime checks still run.

The sensitivity runner now compiles/runs both tests, retains its original 24
controls, and adds migration suppression, lost migration error, skipped record
release, and corrupted migrated kind. Per-tree TMPDIR and a uniform byte-length
preflight prevent unrelated path overflow from becoming a false mutation kill.
Only owned fixture directories are cleaned. Worker diagnostic expectations use
an independent escaping oracle for quotes, backslashes and non-ASCII bytes.

## Full qualification

Command: `CUTILS=/home/travers/projects/c-utils make -C build/listing-qualification/candidate verify`.
The runner archives the immutable dependency commit, not the mutable checkout.
Accepted receipt `run.Nano7T` exits 0 with:

- 6,656 CLI comparisons, seven C test executables, and 34 benchmark unit tests.
- Required subset/lint, compiler matrix, analyzer, ASan/UBSan, MSan, TSan,
  optimized release, WASI, i386, coverage, four semantics harnesses, Valgrind,
  and four bounded 10-second fuzz targets.
- 28 compile-valid behavioral controls rejected. No compilation failure or
  timeout counted as a behavioral kill.
- Authority, regions and CBMC explicitly skipped because they remain
  unconfigured. No required profile was weakened or made optional.

| Module | Covered lines | Percent |
| --- | ---: | ---: |
| `src/coarse.inc` | 406 / 428 | 94.86% |
| `src/fynd.c` | 523 / 534 | 97.94% |
| `src/ignore.inc` | 462 / 477 | 96.86% |
| `src/listing.inc` | 146 / 153 | 95.42% |
| Combined | 1,537 / 1,592 | 96.55% |

The per-module floor remains 90%. The first run, `run.alIR92`, caught a WASI
compile error in the new test fixture's `mkdtemp` call. The fixture now follows
the existing fixed-directory WASI convention. The failed snapshot/log remains;
all required checks were rerun afterward. Native Windows and threaded WASI
execution are not newly claimed; actual unsupported thread backends remain
explicit in the relevant test receipts.

## Local retention and working verification

Four paths were promoted from the exact qualified snapshot:
`src/listing.inc`, `tests/test_listing.c`, `tests/test_listing_worker.c`, and
`tests/sensitivity.py`. Every existing captured source was checked before
writing, new destinations had to be absent, and all 102 qualified inputs matched
afterward. Unrelated work, history, old datasets and failed logs were preserved.

The working binary rebuilt against the qualified dependency and passed seven
C tests, 508 CLI checks, 140 ignore checks, and 34 benchmark tests. Seventy
additional creation-manifest checks compare working and qualified executables
at j1/2/4/8/32, including raw names, duplicate roots, depth, hidden and suffix
selection, migrated listings and worker paths. A smoke-driver bytes/Path typo
was corrected before the passing run; the failed driver receipt remains.

[Focused tiny index](../bench/results/listing-tiny.json),
[tiny full data](../bench/results/listing-tiny-data.json.gz),
[tiny summary CSV](../bench/results/listing-tiny-summary.csv),
[qualification index](../bench/results/listing-qualification.json), and
[qualification evidence](../bench/results/listing-qualification-data.json.gz)
retain the checks, raw paired records, pressure snapshots, source identities,
compiled-control results, failure receipts, promotion manifest and audit scripts.
Same-family source/factual reviewers did not independently rehash raw evidence.

All heavy work ran sequentially under actual 3 GiB/no-swap/nice-15/core-0 limits
and a 5 GiB disk preflight. No cache dropping, affinity or security-policy change
occurred. No total RSS cap, universal fd parity, native CPU-fraction attribution,
whole-program filesystem/scheduling proof, or zero future regression is claimed.

Local evidence audit, without rerunning benchmarks:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; python3 -I build/listing-tiny/analyze.py && python3 -I build/listing-qualification/audit.py && python3 -I build/listing-qualification/final_check.py'
```

Verified for the exercised contracts and bounded qualification above.
