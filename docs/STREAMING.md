# Streaming: scoped fd wins and qualified local retention

Default output is unordered streaming within each root. Two warm
native waves show serial `-j1` about 23% lower elapsed time than fd automatic
threading for flat-million all-file output, and about 27% lower for pipe output.
This is a scoped result, not a promise to always beat fd.

The subsequent [full streaming benchmark](STREAMING_FULL_BENCHMARK.md) covers
107 workloads and 16,002 validated timed outputs. It includes all configured
thread counts, explicit losses, and matched filesystem and ignore controls.
It changes no application code or qualification requirement.

## Contract and implementation

- Root groups remain in argument order. Duplicate and overlapping roots retain
  exact record multiplicity; only ordering inside a root is unspecified.
- Records remain contiguous, including raw embedded LF bytes and NUL-delimited
  output. Selection, ignore precedence, depth and basename filters are unchanged.
- On a later ordinary enumeration EIO, already emitted results remain visible;
  report a diagnostic, return status 1, and continue other reachable work and
  later roots. Resource/output failures still stop work and join workers.
- Open the cursor first to preserve open-error precedence, then load current
  policy before **any** next-entry call. Tests cover late-created policy and
  production `lstat` handling of a dangling `.git` marker.
- Cursor names use a 4 KiB block, a one-byte anchor and per-entry rewind, with
  memory independent of directory width. There is no sort vector, `--sort`
  flag or legacy walker. Entry views expire at next/release; queued paths own
  copies.
- Threaded transport is now the [shared-stack scheduler](SCHEDULER.md). The FIFO
  whole-task transport, its N MiB payload credits and its 128-handle-per-worker
  bound described in this report's measurements were retired with it.

## Native evidence

Portable artifacts: [index](../bench/results/streaming.json),
[compressed data](../bench/results/streaming-data.json.gz), and
[typed summary CSV](../bench/results/streaming-summary.csv).
The index records AFTER_PROMOTION with retained-local status.

Native waves `run-flzvpppk` (offset 0) and `run-_lrvvwx3` (offset 5) each cover
22 cases, nine configurations and nine repetitions: 198 prechecks, 1,782 native
samples, 198 warmups and 162 separate first-byte captures. Retained baseline,
fd-auto and candidate job configurations, all other cases and all reported
comparisons remain in the reports rather than being dropped.

The table uses native `median_ms`, rounded directly from the CSV field to two
decimals, never from a rounded prose value. Each row has nine repetitions.

| run | measurement | case | configuration | median_ms | source |
| --- | --- | --- | --- | ---: | --- |
| run-flzvpppk | native | flat1m-all-file | candidate-j1 | 292.67 | [CSV](../bench/results/streaming-summary.csv) |
| run-flzvpppk | native | flat1m-all-file | fd-auto | 379.44 | [CSV](../bench/results/streaming-summary.csv) |
| run-flzvpppk | native | flat1m-all-pipe | candidate-j1 | 280.12 | [CSV](../bench/results/streaming-summary.csv) |
| run-flzvpppk | native | flat1m-all-pipe | fd-auto | 383.03 | [CSV](../bench/results/streaming-summary.csv) |
| run-_lrvvwx3 | native | flat1m-all-file | candidate-j1 | 294.74 | [CSV](../bench/results/streaming-summary.csv) |
| run-_lrvvwx3 | native | flat1m-all-file | fd-auto | 380.71 | [CSV](../bench/results/streaming-summary.csv) |
| run-_lrvvwx3 | native | flat1m-all-pipe | candidate-j1 | 283.02 | [CSV](../bench/results/streaming-summary.csv) |
| run-_lrvvwx3 | native | flat1m-all-pipe | fd-auto | 390.24 | [CSV](../bench/results/streaming-summary.csv) |

Separate first-byte captures for flat1m-all-pipe have
`observed_first_median_ms` 1.139719 / 1.134161 for candidate-j1 versus
2.592183 / 2.482213 for fd-auto, in wave order. These include launch, stdio,
observer and scheduling effects; they are not pure walker latency. Native
flat1m candidate-j1 median peak RSS is 1,728 KiB (about 1.69 MiB), versus about
39 MiB for the retained baseline. First-byte captures have no native CPU/RSS
measurements and are not pooled with native timing rows.

Paired bootstrap intervals (4,000 resamples, seed 7) have nominal upper bounds
below 1 for paired candidate/fd elapsed-time ratios in both primary sinks and both waves.
This is not machine isolation, statistical independence or a multiple-comparison
correction, nor evidence of parity with every fd configuration.

## Tiny-query adjudication and counts

The original three `tiny-none-file` flags at j1/j4/j16, roughly 5-11%, remain
recorded. A fixed, preregistered follow-up used two waves, 36 repetitions, 12 labels and
six cases per wave, with seeds 3109/6203 and exact baseline-check aliases.
Each wave has 72 prechecks, 2,592 timed samples and 216 warmups. Resolution is
`CLEAR_FOR_SEPARATE_QUALIFICATION`: no repeated 5% regression flag under that
protocol. Non-reproduction is neither zero regression nor proof of noise.

Combined totals are 540 prechecks, 8,748 standard native timings, 828 warmups
and 324 first-byte captures. The CSV has 576 typed groups: 396 `native`,
144 `tiny_native`, 36 `first_byte`. Nine- and 36-repetition groups must never
be pooled. The 6,656 CLI functional checks below are not benchmark timings.

## Qualification, failures and local retention

Selected full receipt:
[`run.vbgYhn`](../build/streaming/qualification/build/verification/run.vbgYhn/).
Its exit is 0; required deep profiles pass: subset, lint, matrix, analyzer,
sanitizers, MSan, TSan, release, wasm, i386, coverage, semantics, Valgrind and
fuzz. Six active C programs, four semantics and four fuzz targets are retained;
qualification includes 6,656 CLI checks and 34 benchmark tests. Optional
`authority`, `regions` and `cbmc` are explicitly unconfigured, not passes.

[Module coverage](../build/streaming/qualification/build/verification/run.vbgYhn/modules.json)
is coarse 406/428 (94.86%), main 531/548 (96.90%), ignore 471/491 (95.93%),
listing 80/87 (91.95%); combined 1,488/1,554 (95.75%). Every module exceeds the
unchanged 90% floor. All 25 compiled behavioral controls reject: 19 retained
and six new. Nine obsolete contract controls are explicitly retired, not scored.

Failure chronology remains visible:
- The old implementation compiled but failed the before-EOF visibility test
  (RED, exit 134) after byte-fixture cleanup. The first cursor failed a 64 KiB
  held-byte assertion because of its 64 KiB names block plus header; the fix was the
  4 KiB block and anchor, not a higher budget. Both fixes preceded native timing.
  The first failed cursor source was not snapshotted; its log and fix history remain.
- Original full `run.bzg9ZI` passed profiles/coverage but one control failed to
  compile, not behaviorally reject. Only the control changed, replacing the
  constant-false conjunct with `&& path[0] == 0`; full rerun `run.t4MK23` passed.
- PRE139 then detected a misplaced root `bench/README.md` edit made after all
  timings. The edit was preserved, the original root file restored, qualified
  documentation corrected, and a third complete run, `run.vbgYhn`, passed.
  [Recovery receipt](../build/streaming/documentation-recovery.json).

The final eight application/build inputs are byte-identical to the native candidate
(six C/header/CLI inputs plus Makefile and the dependency revision);
qualification changed documentation/tests, not the native performance patch.
[Promotion](../build/streaming/promotion.json) retained 23 changed paths with all
102 qualified inputs matching root. Six obsolete root C/fuzz/semantics files
are preserved under `build/streaming/retired-root-contract-tests`, not deleted
from the evidence. Historical results and unrelated changes remain untouched.

The pinned working rebuild passed six C suites, 508 CLI checks, 140 ignore
checks and 34 benchmark tests ([working log](../build/streaming/working.log)).
The [working smoke receipt](../build/streaming/working-smoke.json) reports
70/70 successful checks across working and selected qualified binaries, five
job settings and seven cases. PRE and POST source/evidence audits pass.
Retention is local only: no commit, push, install or publication.

## Provenance and limitations

Every actual measured output was validated as a NUL-record Counter with exact
multiplicity; root groups were checked separately. Canonical actual-output
hashes are distinct from ordered baseline hashes. Independently created
manifests use component-wise sorted DFS as canonical oracle order, not a
promise about current output order. Successful raw scratch outputs were reused;
retained hashes/counts do not enable retrospective rehashing of every sample.
Frozen inputs and build logs provide provenance, not cryptographic binary/source
attestation. Read-only review used the same model family, not independent rehash.

Runs used 3 GiB/no swap, nice 15, core limit 0 and a 5 GiB disk preflight.
No cache dropping, affinity, security-policy or dependency changes were made.
Fixtures and validation preheat caches: these are warm, non-isolated results,
not disk-cold measurements. Timing includes launch, output I/O and scheduling;
RSS is not aggregate process-tree memory. Neither total-RSS bounds, full cache
residency nor universal fd superiority follows from these measurements.
