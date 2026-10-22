# Verification evidence

**Verified for the documented contracts, within the limits below.** This is
behavioral evidence, not a whole-program proof.

The subsequent [full streaming benchmark](STREAMING_FULL_BENCHMARK.md) validates
1,778 prechecks, 16,002 timed outputs, and 963 warmups across 107 workloads.
Its source identities still match the selected qualification below. Benchmark
artifact audits and CPU-forgery controls are separate from release qualification.

## Current source: final-component ignore matching

Receipt: `build/verification/run.YOdmVS`, exit 0. It qualifies the
[final-component ignore matcher](IGNORE_MATCHER.md), the current working source.
Build guards, six C suites, 6,324 CLI checks across seeds 1, 7 and 42, 332 ignore
CLI checks, 8 validated benchmark-harness outputs and required deep checks pass.
Authority, regions and CBMC remain optional skips. All 26 named behavioral controls
reject, including the new `skip-frames-without-plain-rules`. Coverage: coarse
94.86%, main 96.90%, ignore 95.89%, listing 91.95%; combined 95.74%.

Only `src/ignore.inc`, its C and fuzz tests, and `tests/sensitivity.py` differ
from the streaming acceptance below. The active `build/fynd` is still the
streaming-selected executable.

## Prior unordered-streaming acceptance

Selected receipt:
`build/streaming/qualification/build/verification/run.vbgYhn`, exit 0.
The [streaming report](STREAMING.md) records the approved unordered/partial-result
contract, matched fd-auto comparison, fixed tiny-control follow-up, full
qualification, local promotion and working-copy checks.

Required compiler, analyzer, sanitizer, MSan, TSan, release, WASI, i386,
coverage, semantics, Valgrind and bounded fuzz checks pass. There are 6,656 CLI
checks, six C executables, 34 benchmark tests, four semantics targets and four
10-second fuzz targets. Optional authority, regions and CBMC remain explicitly
unconfigured. All 25 compile-valid behavioral controls reject; nine obsolete
sort/listing controls are retired by contract, not counted as kills.

Coverage remains above the unchanged 90% per-module floor: coarse 94.86%,
main 96.90%, ignore 95.93%, listing 91.95%; combined 95.75%. All 102 selected
qualification inputs match the working tree. The measured application/build
inputs remain byte-identical; qualification adds test evidence, not another
runtime performance patch.

The working rebuild passes six C tests, 508 CLI, 140 ignore and 34 benchmark
checks, plus 70 independent working/qualified smokes at j1/2/4/8/32. Root groups
and duplicate multiplicity remain strict; output order within a root is not.
No commit, push, install or publication occurred.

The sections below are historical. Their source-identity counts refer to their
own retention points, not the current source tree.

## Historical large-listing acceptance

Historical selected receipt:
`build/listing-qualification/candidate/build/verification/run.Nano7T`, exit 0.
The [listing qualification report](LISTING_QUALIFICATION.md) retains the earlier
memory result and noisy tiny flag, two preregistered unchanged-binary follow-up
waves, added failure evidence, full qualification and local promotion receipts.

All 6,656 CLI comparisons, seven C executables, 34 benchmark tests and required
compiler/analyzer/ASan-UBSan/MSan/TSan/release/WASI/i386/coverage/semantics/
Valgrind/fuzz checks pass. All 28 compile-valid behavioral controls are rejected;
no compile failure or timeout counts as a kill. Optional authority, regions and
CBMC remain explicitly unconfigured. Every module remains above the 90% floor:
coarse 94.86%, main 97.94%, ignore 96.86%, listing 95.42%; combined 96.55%.

All 102 qualified inputs match the working tree after four-path local retention.
The application sources are identical to the measured memory-saving candidate.
The rebuilt working binary passes seven C tests, 508 CLI, 140 ignore and 34
benchmark tests, plus 70 independent working/qualified exact-byte smokes at
j1/2/4/8/32. No commit, push or publication occurred.

The later [listing-latency candidate](LISTING_LATENCY.md) is not retained.
Narrow behavior and targeted ASan/UBSan checks pass, but native benefit and
control gates fail. Full qualification was not run for that candidate.
The [collection attribution](COLLECTION_COST.md) then measured the unchanged
retained executable and a private diagnostic probe. It proposes no application
candidate and did not replace that qualification at the time.

Earlier sections below are historical receipts. Their source-identity counts
refer to their own promotion/investigation points, not a claim that old source
snapshots still match every current file.

## Shared-credit and ignore-filter acceptance

Command: `CUTILS=/home/travers/projects/c-utils make -C build/performance-pass/combined verify`.
Selected receipt: `build/performance-pass/combined/build/verification/run.FyCw0V`,
exit 0. The pin, required profiles, resource caps, and 90% per-module floor stayed
unchanged. All 6,656 CLI comparisons, five C executables, 34 benchmark unit tests,
and required deep checks passed, including MSan, TSan, i386, WASI, semantics,
Valgrind, and bounded fuzzing. Authority, regions, and CBMC retain optional skips.

All 24 compile-valid behavioral controls were rejected. Added controls expose
reservation theft, incorrect IO ownership, failed-IO recycling, and wrong or
absent necessary-first-byte filtering. Compile failures and timeouts do not count.
The shared-credit suite checks fixed ownership conservation, a head reserve with
all shared credits occupied, wakeup of another producer, pinned actual IO bytes,
private/queued/IO cancellation, exact tails, mixed channels, and slot reuse.
Independent ignore oracles and fuzzing cover escape/slash boundaries, high bytes,
exact spans, precedence, directory rules, and charged-work limits.

| Module | Covered lines | Percent |
| --- | ---: | ---: |
| `src/coarse.inc` | 406 / 428 | 94.86% |
| `src/fynd.c` | 522 / 534 | 97.75% |
| `src/ignore.inc` | 462 / 477 | 96.86% |
| `src/listing.inc` | 125 / 133 | 93.98% |
| Combined | 1,515 / 1,572 | 96.37% |

All 100 captured inputs matched the eight-path promotion. The working binary
rebuilt against the qualified archive passed five C tests, 508 CLI checks,
140 ignore checks, and 34 benchmark tests. Another 23 independent exact-byte
checks passed at j1/2/4/8/32, including duplicate roots and ignore ancestry.
The selected exact qualified binary
passed 363 prechecks, 3,267 native timed-output validations, and 297 separate
validated warmups. A separate fully qualified 64 KiB variant remains an ablation.
The new default is 256 KiB batches within the same aggregate N MiB payload,
not smaller dedicated mailboxes. N pending includes DONE/unread/current delivery.
See [results and limits](PERFORMANCE_PASS.md), including regressions and replay.

The later [focused investigation](REGRESSION_INVESTIGATION.md) passed 96 prechecks,
2,304 timed exact-byte validations, and 384 warmups without application changes.
It did not reproduce the earlier large smaller-tree/serial regressions. Existing
qualification still matches all 100 captured inputs; no new code qualification
is implied by diagnostic timings.

## Rejected bounded skew prototype

The isolated [nested-worker experiment](SKEW_TRAVERSAL_EXPERIMENT.md) passed strict
native builds, six C executables, 508 CLI checks, 140 ignore checks, and a targeted
fast check. New barriers exercise nested overlap, token/credit conservation,
relay/main IO pinning, ordered errors, scoped cancellation before parent cleanup,
and transactional seed failures. Queued local cancellation and deeper relay
chains remain test gaps.

Both native runs passed 288 combined prechecks, 2,592 timed-output validations,
and 288 warmups. The prototype failed its declared smaller-tree regression gate
and did not improve the extra-level skew control. It was not promoted or fully
profile-qualified. No sanitizer/platform/coverage/mutation claim is made for it.
All 105 pre-experiment production inputs remain unchanged.

## Rejected continuation prototype

The isolated [frontier experiment](FRONTIER_TRAVERSAL_EXPERIMENT.md) passed strict
native builds, six C executables, 508 CLI checks, 140 ignore checks, and targeted
fast checks. New tests cover deep-prefix consumption and redistribution without
parent workers, unique outer remainders, reference ownership, split-allocation
rollback, exact raw/ignore-aware bytes, short-write cancellation, and joined
cleanup with zero active frames/cursors.

Two runs passed 288 prechecks, 2,592 timed-output validations, and 288 warmups.
Both skew gains exceeded the target, but smaller/quiet controls failed the
retention gate. No full sanitizer/platform/coverage/fuzz/mutation qualification
or complete new live-listing-bound evidence is claimed. No promotion occurred;
all 108 pre-experiment production inputs remain unchanged.

## Rejected private-leaf reduction

The [profile-driven follow-up](FRONTIER_COST.md) has a compile-valid failing-before
sensor: exact bytes/status pass, then one persistent leaf publication violates a
zero-publication budget and aborts with status 134. After the isolated reduction,
that sensor, seven C executables, 508 CLI checks, 140 ignore checks, and targeted
fast checks pass. Private-builder exclusivity/empty-arena/fault evidence remains
incomplete; no full qualification is claimed.

Two matched matrices pass 320 prechecks, 3,200 timed-output validations, and 320
warmups. Two focused profiles add 144 native samples and 24 validated instrumented
outputs. The native gate still fails, so no promotion occurs. All 111 captured
production inputs remain unchanged. Sanitizer/platform/coverage/mutation floors
were not weakened or bypassed for retained code.

## Isolated reclamation reduction

The [disposal-only follow-up](FRONTIER_RECLAIM.md) passes eight strict C
executables, 508 CLI checks, 140 ignore checks, and targeted fast checks.
A paused-disposal model proves another thread can acquire the scheduler mutex
while zero-reference payload controls remain charged and unavailable for reuse.
It checks three ancestors and joined teardown. A compile-valid locked-disposal
control aborts on the strengthened assertion before its barrier, not by timeout.

Two native runs pass 320 prechecks, 3,200 timed-output validations, and 320
warmups. Twelve separately validated diagnostic outputs measure the old locked
spans, with clock overhead and no native CPU-fraction claim. All 114 captured
production inputs remain unchanged. Quiet-small medians exceed the unchanged
5% gate in both runs; no promotion or full qualification follows. Live-pop
cancellation/reuse and complete peak listing-bound evidence remain incomplete.

## Isolated redundant-poll subtraction

The [one-poll follow-up](FRONTIER_POLL.md) preserves directory-entry/every-64,
leaf-start, promotion, publication and delivery checks. Its compile-valid sensor
passes exact bytes/status, injected-stop cases and cleanup before failing a
three-poll budget at four. The unchanged sensor passes at three after subtraction.
Nine C executables, 508 CLI checks, 140 ignore checks, and fast checks pass.

Two matched runs pass 320 prechecks, 3,200 timed-output validations, and 320
warmups. Smaller-tree native controls still exceed the unchanged 5% gate in
both runs. No promotion or full qualification occurs; all 117 captured production
inputs match. Empty-child cancellation with a live parent/FILL, hidden/ignore
cases and the preceding ownership/listing-bound gaps remain incomplete.

## Isolated stop-read synchronization

The [final contention pass](FRONTIER_CONTENTION.md) retains one stop flag and
every poll. Slot readers use independent guards; the running writer holds the
scheduler mutex and all initialized guards across the write. The old strict-
compiled sensor aborts with status 134 before blocking on the scheduler mutex.
The same sensor passes afterward, proving progress while that mutex is held,
distinct guards, stop visibility, repeated joins and startup-failure cleanup.
Nine C executables, 508 CLI checks, 140 ignore checks, and fast checks pass.

Two native matrices pass 320 prechecks, 3,200 timed-output validations, and 320
warmups. A separate focused pass adds eight prechecks, 40 native timings, 20
warmups and 24 validated diagnostic outputs. Diagnostic counts/timing are not
native CPU fractions. Quiet j8 controls approach production, but repeated
smaller-tree j16 regressions fail the unchanged gate. No promotion or full
qualification occurs; all 120 captured production inputs match. Concurrent
writer-exclusion sensitivity and earlier ownership/listing gaps remain open.
Continuation optimization stops here rather than restricting tested threads.

## Isolated large-listing storage

The [large-listing experiment](LARGE_LISTING.md) activates a stable record arena
only beyond 1,024 entries, retaining copied names and the small-list allocation
path. Its strict-compiled old sensor checks order/payload/path and cleans owned
fixtures before aborting 134 on a normal-arena allocation budget. The candidate
passes, with independent record/name failure cases, migration/later-growth and
limit failures, no sort on error, and both arenas empty after cleanup.
Six C executables, 508 CLI checks, 140 ignore checks, and fast checks pass.

Two matched native matrices pass 378 prechecks, 3,402 timed-output validations,
and 378 warmups. The separate baseline adds 36 prechecks, 180 timings, 45 warmups
and 12 diagnostic outputs. Million-file RSS improves about 38%, but tiny j8
point medians flag the declared 5% control gate in both runs. Paired evidence
is noisy, not proof of an intrinsic penalty. No promotion/full qualification
occurs. Post-migration name faults and worker record-ENOMEM drain/abort tests
remain gaps before qualification; required profiles/floors are unchanged.

## Coarse ordered traversal acceptance

Command: `CUTILS=/home/travers/projects/c-utils make -C build/coarse-traversal/prototype verify`.
Receipt: `build/coarse-traversal/prototype/build/verification/run.XXAMxe`, exit 0.
The runner archived the immutable dependency pin, not the live dependency source.
Its unchanged 3 GiB/no-swap/nice-15/no-core limits and required checks remained.

All 6,656 CLI comparisons, four C executables, six benchmark-runner tests, and
required deep profiles passed. These include compiler/lint/analyzer,
ASan/UBSan, MSan, TSan, optimized/i386/WASI, Valgrind, four bounded semantics
cases, and four fuzz targets. Authority, regions, and CBMC retain their explicit
optional skips. This is not a whole-program filesystem or scheduling proof.

The rewritten thread suite tests real directory overlap, full future mailboxes
while the head progresses, exactly full final tails without consumer ACK,
DONE-inclusive global capacity, channel transitions and explicit flushes,
oversized raw paths/escaped diagnostics, ordinary versus terminal worker errors,
startup primitive and pool-arena allocation sweeps, and repeated cancellation.
A coordinator output-failure fixture holds workers borrowing parent ignore rules
and checks joining before any affected CuTemp scope ends.

All 19 compiled behavioral controls were rejected. Obsolete listing-prefetch
controls were replaced with publication-condition failure, lost-final-tail,
and pending-bound controls. The suppressed-thread-start control now asserts
worker entry before reaching the overlap barrier, so it rejects behaviorally
rather than merely timing out. No compile failure or timeout counted as a kill.

| Module | Covered lines | Percent |
| --- | ---: | ---: |
| `src/coarse.inc` | 298 / 307 | 97.07% |
| `src/fynd.c` | 523 / 534 | 97.94% |
| `src/ignore.inc` | 431 / 450 | 95.78% |
| `src/listing.inc` | 125 / 133 | 93.98% |
| Combined | 1,377 / 1,424 | 96.70% |

All 61 captured inputs matched after promotion. The exact qualified binary then
passed 126 prechecks and 1,134 native timed-output validations. Every fynd output
also matched the previous qualified serial bytes. The smaller-mailbox ablation
was not retained or separately full-profile-qualified. Historical source,
failed targeted checks, exploratory measurements, and earlier receipts remain.
See [coarse traversal results](COARSE_TRAVERSAL.md) for gains, memory costs,
skew limitations, source/driver snapshots, and replay constraints.

## Sort optimization acceptance

Latest command: `make verify`, receipt `build/verification/run.cjDJRE`, exit 0.
All 6,656 CLI checks, four C executables, six benchmark-runner tests, and required
deep profiles passed. These include compiler/analyzer, ASan/UBSan, MSan, TSan,
optimized/i386/WASI, Valgrind, four bounded semantics cases, and four fuzz targets.
The same authority, regions, and CBMC checks explicitly skipped.

The new sorting tests compare against independent byte/length oracles, retain
record identity and payloads, and cover raw bytes, equal keys, cutoff boundaries,
reverse/organ-pipe inputs, and long prefixes. Exact-size fuzz allocations expose
string overreads. Invalid sentinel identities make missing builder writes
observable. All 18 compiled behavioral controls were rejected, including four
new sort correctness/work-limit controls. No timeout counted as a rejection.

| Module | Covered lines | Percent |
| --- | ---: | ---: |
| `src/fynd.c` | 472 / 483 | 97.72% |
| `src/ignore.inc` | 431 / 450 | 95.78% |
| `src/listing.inc` | 266 / 273 | 97.44% |
| Combined | 1,169 / 1,206 | 96.93% |

All 59 captured inputs matched their hashes after acceptance. The exact
qualified binary then passed 98 benchmark prechecks and 846 timed-output
validations across the primary and extra filename distributions. Gains were
concentrated in wide directories; long shared prefixes regressed slightly.
See [sort optimization results](SORT_OPTIMIZATION.md) for the tradeoff.

The working executable was rebuilt against the accepted archive. All C tests,
508 CLI checks, 140 ignore checks, and eight exact-byte j1/j32 smoke checks passed.

## Rejected scheduling experiment

The early-refill deletion was tested in isolated snapshots and rejected after
native branching workloads regressed. At that point, production still matched
all 49 inputs of `run.B1HO3G`; the experiment did not change those inputs. It
validated 72 prechecks, 648 native samples, and 96 instrumented outputs, with
independent manifests and exact successful fynd ordering comparisons. No full
checker or sanitizer qualification was claimed for the discarded candidate.
See [the experiment and reproduction details](SCHEDULING_EXPERIMENT.md).

## Walk optimization acceptance

Command: `make verify`, receipt `build/verification/run.B1HO3G`, exit 0.
The run retained the immutable c-utils pin, resource caps, required checks, and
90% per-module floor. All 6,656 filesystem/thread/ignore subprocess checks,
three C executables, six benchmark-runner tests, and required deep checks passed,
including ASan/UBSan, MSan, TSan, optimized/i386/WASI, Valgrind, semantics, and
bounded fuzzing. Authority, regions, and CBMC remain explicit optional skips.

The thread suite now deterministically holds a running collector until after
cancellation, waits for terminal publication, and checks token release and join
before destroying synchronization. Startup injection covers the mutex, both
conditions, and both workers. All 14 compiled behavioral controls were rejected;
the added control ignores completion-condition initialization failure.

| Module | Covered lines | Percent |
| --- | ---: | ---: |
| `src/fynd.c` | 472 / 483 | 97.72% |
| `src/ignore.inc` | 431 / 450 | 95.78% |
| `src/listing.inc` | 191 / 198 | 96.46% |
| Combined | 1,094 / 1,131 | 96.73% |

All 49 captured inputs still matched their hashes after acceptance. Separate
performance checks validated 80 prechecks and 720 native measurements, plus
12 Callgrind runs and 18 traced runs. Their small native gains do not establish
universal speedups. See [walk optimization results](WALK_OPTIMIZATION.md).

The working binary was rebuilt against the accepted archive. Its C tests,
508 CLI checks, 140 ignore checks, and eight exact-byte j1/j32 smoke checks
passed. The initial smoke omitted the visible `.ignore` file from its expected
file list. Correcting that expectation and checking explicit extension filters
required no application change; the original failure log is retained.

## Original ignore and thread acceptance

Command: `make verify`, receipt `build/verification/run.2Qgzaa`, exit 0.
The run used the unchanged 3 GiB/no-swap/nice-15/no-core scope and pinned c-utils
revision. It passed 6,656 fynd subprocess checks, including exact ordered
serial/j2/j4/j8 comparisons and 64 seeded Git/fd subset fixtures. All three C
executables, compiler/analyzer/sanitizer profiles including TSan, optimized/i386,
WASI, Valgrind, three pure semantics cases, and three bounded fuzz targets passed.
Authority, regions, and CBMC remain the same explicit optional skips below.

The private thread suite proves two child-directory collectors overlap under
one root using a mutex/condition barrier, not a timing threshold. It also checks
global pending limits including completed jobs, synchronous fallback with later
siblings occupying capacity, cancellation, startup-failure injection, worker
allocation/listing faults, and successful subsequent invocations. Native
startup failure cannot silently skip the overlap test. WASI exercises serial
fixtures and requested-thread failure rather than pretending to start workers.

The checker normally reports only `.c` files in its coverage table. A new
project gate separately includes `.inc` modules and requires each module to
reach the unchanged 90% line floor. Actual results are:

| Module | Covered lines | Percent |
| --- | ---: | ---: |
| `src/fynd.c` | 467 / 478 | 97.70% |
| `src/ignore.inc` | 431 / 450 | 95.78% |
| `src/listing.inc` | 185 / 192 | 96.35% |
| Combined | 1,083 / 1,120 | 96.70% |

Its four planted controls reject low, missing, empty, and duplicate module
coverage. All 13 compiled behavioral perturbations were rejected. New sensors
cover ignore disabling/pruning, suppressed worker startup, and repeated prefetch
scans. The latter reverts the monotonic scan cursor and fails a 256-file
operation-count assertion, exposing quadratic rescan behavior without timing.

Independent review/probes also found and fixed physical `symlink/..` ancestry:
ignore matching now reads the actual native directory's rules while preserving
display spelling. The original probe failed; native C and Python regressions
now pass. A fault-injection test was corrected to reach a real allocation
attempt rather than an already-used arena block. Test-only C89 initialization
and WASI fixture issues were corrected without suppressions or weaker gates.

The working checkout was rebuilt against the accepted pin. Its C tests, 508
filesystem/thread CLI checks, 140 ignore checks, and a corrected maximum-32-worker
smoke passed. The first ad hoc smoke expected only `.c` output without requesting
an extension filter; correcting that expectation required no application change.

Separate one/four-worker benchmarks passed 36 prechecks and 180 measured-output
validations. They did not show a reliable warm-cache speedup; serial remains the
default. See [benchmark results](BENCHMARKS.md) and
[ignore/thread contracts](IGNORE_THREADS.md) for semantics and remaining limits.

## Baseline follow-up

`make verify` passed again with receipt `build/verification/run.wVWrW0`, exit 0.
Production source is unchanged. This run adds permanent exact-byte diagnostic
assertions, incomplete-listing continuation to a later leaf root, native paths
over 1,024 bytes, and an independently probed over-PATH_MAX root failure with
untruncated diagnostics and continuation. Unsupported host path facilities
report explicit skips; none of these path checks skipped on this host.

The updated suite passed 1,692 CLI comparisons, six benchmark-runner unit tests,
and all deep checks required below. Source line coverage remains 97.55%.
The same three optional checks remain skipped. Nine compiled source perturbations
were rejected behaviorally: the original six plus raw diagnostic bytes,
erroneously terminal listing EIO, and joined-path truncation at 1,023 bytes.

The new benchmark runner was then run separately under the resource caps.
All 18 prechecks and 90 measured outputs passed against creation manifests.
See [the recorded baseline](BENCHMARKS.md),
[procedure](../bench/README.md), and
[result data](../bench/results/serial-baseline.json).

## Original accepted run

Command: `make verify`

Local receipt: `build/verification/run.z3uJ2z`, exit 0. The receipt contains
`deep.json`, `cli.json`, `source-hashes.json`, the dependency revision, isolated
source trees, checker logs, and sensitivity results. Generated artifacts are
ignored rather than committed. Rerun the command to produce a new receipt.

The accepted dependency is
`eafa795805fa997e2db91f9182c4e0f86fa32fb4`. Verification extracted that Git
revision without modifying the sibling c-utils working tree.

The whole run used a systemd user scope with MemoryMax=3G,
MemorySwapMax=0, nice 15, and core dumps disabled. Checks ran sequentially,
except for the two-job native compile. No floor or diagnostic was suppressed.

| Check | Observed result |
| --- | --- |
| Strict native build and C tests | Passed. C89 with pedantic errors and warnings treated as errors. |
| Independent CLI oracle | 1,671 subprocess comparisons passed, 557 each at seeds 1, 7, and 42. Each seed generated 500 cases in addition to fixed fixtures. |
| Subset, lint, compiler matrix, static analyzer | Passed. |
| ASan/UBSan, MSan, TSan | Passed. The application remains serial; TSan is not evidence of a parallel implementation. |
| Optimized/LTO release and i386 | Passed. |
| WASI via Wasmtime | Passed filesystem, allocation, parser/filter, depth, and stream-error fixtures. FIFO and Linux `/dev/full` cases are unavailable there. |
| Source coverage | 97.55% lines, 95.42% regions, 87.50% branches in `src/fynd.c`; all 14 functions exercised. The line floor remains 90%. |
| Cerberus | Passed two dedicated pure-operation/parser cases. Full native filesystem tests are not interpreted by these cases. |
| Valgrind | Passed. |
| Bounded fuzzing | Both targets passed with 512-byte inputs and configured 10-second budgets. The runner reported 11 seconds per target. No long-duration campaign was run. |

The receipt checker requires every listed deep check to pass. Its self-test
rejected four broken controls: overall failure, a missing required check,
a skipped required check, and a duplicate check.

Three checks are explicit skips, not passes:

- Authority checking: no `ward.toml`.
- Region checking: no `REGIONS_CHECK` configured.
- CBMC: no proof cases in `proofs/`.

The checker receipt records `strict: false` because these optional checks
are absent. The acceptance wrapper separately rejects missing required checks
and unexpected skips.

## Test sensitivity

`tests/sensitivity.py` first built and passed an unmodified baseline. It then
compiled six isolated source perturbations. All six compiled successfully
and were rejected by behavioral C/CLI tests, without a timeout:

- suppress basename filtering;
- ignore the type filter;
- reverse directory ordering;
- include hidden names by default;
- follow explicit root symlinks;
- disregard delayed output-flush failure.

These are six targeted checks, not a code-wide mutation score. Results and
per-command logs are in the receipt's `sensitivity/` directory. The runner
rejects compilation failures and timeouts as inconclusive rather than
counting them as successful behavioral rejection.

An earlier independent public-entry probe exposed a real bug: an output
stream with an existing error incorrectly returned success. The original
probe failed with exit 134. The corrected implementation preserves the
stream error and returns 1; native C tests retain the regression.

## Claim-to-test map

| Contract | Evidence |
| --- | --- |
| Literal basename, ASCII folding, type/extension unions, and grammar | C parser/helper cases, independent Python byte comparisons, parser fuzzing. |
| Root order, duplicate roots, sorted preorder, hidden policy, and filters that do not prune | Ordered CLI fixtures and generated trees; ordering/hidden/type perturbations. |
| Explicit files, broken links, symlink leaves, and FIFO entries | Native C and CLI fixtures; root-link perturbation. |
| Raw paths and NUL records | Exact byte-output comparisons, including non-UTF-8 bytes and embedded filename newlines. |
| Checked path arithmetic | Last-fit/first-overflow cases, unchanged output on arithmetic failure, independent subtraction-based fuzz oracle. |
| Arena lifetime and cleanup on allocation failure | Deterministic fail-at sweep, byte limits, successful subsequent calls, sanitizers and Valgrind. |
| Discard incomplete listings | Injected CuDir iteration failure before any collected child is visited. |
| Stop on observed output/diagnostic failure and detect final flush failure | Stream-error fixtures, the original failing probe, flush perturbation. |
| Explicit depth cutoff versus default safety failure | Native and CLI boundary fixtures at depth 128. |

Private fault tests include the production source with a renamed entry so
checker builds can also link the ordinary source without duplicate symbols.
The separate CLI oracle executes the real built executable.

## Working-checkout smoke test

After acceptance, `make -j2 all test cli-test` passed under the same resource
limits and produced `build/fynd`. The default CLI suite passed 157 comparisons.
The version command printed `fynd 0.1.0`. Additional exact-byte probes passed
for documented examples, NUL output, escaped diagnostic path bytes, and
continuation to a later root after a missing root.

Local log: `build/verification/native-final-fixed.log`, exit 0. An earlier
ad hoc smoke assertion had an incorrectly quoted expected diagnostic; fixing
the probe required no application change. Earlier failed checker runs remain
in their own directories and are not the acceptance receipt.

## Remaining limits

- The recorded comparison covers synthetic warm-cache regular-file workloads
  only. It is not a real-repository, cold-cache, or default-parallel-fd comparison.
- No native Windows qualification, filesystem race proof, or sandbox guarantee.
  Native ignore ancestry is physical; WASI/Cerberus retain a lexical fallback.
  Git config/worktree gitdir fidelity and complete Git pattern compatibility
  are not claimed. Source and matcher resource limits are explicit.
- No directory-close error guarantee because CuDir does not expose it.
- Long paths are now tested on the native host. Equivalent filesystem path
  qualification on other targets remains open.
- Coverage leaves unexecuted branches. Fuzzing is short and bounded; neither
  coverage nor a successful interpreter run establishes exhaustive correctness.
- The dependency was pinned and compiled with the application. This run is
  not a new full qualification of every c-utils module.
