# Directory collection cost on the retained build

Historical attribution of the sorted-listing baseline. The subsequent
[user-approved streaming implementation](STREAMING.md) changes the ordering and
partial-result contract; this report does not describe the current runtime.

Decision: `STOP_NO_JUSTIFIED_APP_CANDIDATE`.

The attribution pass does not justify an application change for the declared
15% latency target. Production remains the [qualified record-arena build](LISTING_QUALIFICATION.md),
`run.Nano7T`, with its roughly 39 MiB million-file peak RSS. No candidate,
promotion, new mutation campaign, or full qualification was attempted.

## What the measurements distinguish

The preceding [latency experiment](LISTING_LATENCY.md) found collection larger
than sorting on plain million-file names. This pass adds user/system CPU deltas
to a private j1 phase probe and records two separate Callgrind observations.
It does not add per-entry clocks or change the pinned library.

Across four instrumented million-file observations:

| Phase metric | Range, ms |
| --- | ---: |
| Collection CPU | 257.880 to 265.033 |
| Collection user CPU | 15.269 to 22.982 |
| Collection system CPU | 240.237 to 249.764 |
| Sort CPU | 100.239 to 108.148 |

The probe samples `getrusage(RUSAGE_SELF)` at phase boundaries. Collection ends
before inventory work; sort timing starts after inventory. Name and record
arenas remain separate and correctly counted. Inventory is excluded from both
intervals but can still affect subsequent cache state.

These are instrumented phase measurements, not native CPU fractions or direct
attribution to a particular kernel operation. They do not prove an Amdahl bound.
The process-wide native timings below are separate distributions. Their user
and system medians need not belong to the same invocation.

Receipt values from five native repetitions, in raw CSV units:

| case | configuration | metric | value |
| --- | --- | --- | ---: |
| flat1m-all-file | fynd-j1 | median_ms | 474.63 |
| flat1m-none-file | fynd-j1 | median_ms | 410.03 |
| flat1m-none-file | fynd-j1 | user_cpu_ns | 172899000 |
| flat1m-none-file | fynd-j1 | system_cpu_ns | 242642000 |

The native j1 all-results median RSS is about 38.7 MiB. Native j8 and fd-auto,
quiet searches, file/pipe sinks, and shared-prefix names remain in the report.
First-byte captures use the previously tested raw-read observer and stay separate
from native CPU/RSS samples. No comparison establishes universal fd parity.

## Instruction counts do not measure kernel cost

Callgrind records j1 quiet searches on flat100k and shared-prefix20k fixtures.
Both actual outputs validate. The optimized collector is inlined, so its
annotation parent is `src/listing.inc:visit`, not a standalone collector symbol.
Selected child edges of that same parent are:

| Child edge | Flat100k instructions | Calls | Shared-prefix20k instructions | Calls |
| --- | ---: | ---: | ---: | ---: |
| `cu_dir_next` | 22764020 | 100001 | 5833131 | 20001 |
| `cu_vec_push_` | 5130332 | 101024 | 1191180 | 21024 |
| `cu_vec_failed` | 500005 | 100001 | 100005 | 20001 |

The push counts include the one-time 1,024-record migration. Enumeration/name-copy
instruction work exceeds these append edges in both observations. Nested callees
must not be added again. Recursive-inclusive radix annotations can exceed the
whole-program instruction total; they are not extra observed instructions.
Callgrind does not count the kernel implementation's work, and these instruction
counts are not native CPU fractions or hardware-cycle attribution.

Source grounding confirms that `cu_dir_next` calls `readdir`, classifies entries,
and copies names into stable arena storage. The library owns that boundary.
This pass does not replace it with direct Linux enumeration calls or edit the
immutable c-utils revision.

## Why no application patch follows

The pinned library provides `CU_VEC_PUSH_FAST`. It preserves NULL, element-size,
scope, sticky-failure, and capacity checks, with ordinary push handling growth
and other cases. It is a possible safe implementation choice, not evidence of
a 15% end-to-end gain. Claiming that no safe small change exists would be wrong.

The observations do not justify expecting that gain from checked append changes.
Bypassing guards, changing compiler defaults, or replacing enumeration would
also introduce risks that this attribution does not justify. Rather than build
a speculative candidate, this pass stops with production unchanged. It does not
prove that collection can never improve.

## Evidence and limits

`build/collection-cost/profile-09o5mlvb` contains 27 prechecks, 135 standard native
timings, 45 warmups, 27 observed first-byte captures, 12 diagnostic phase outputs,
and 2 Callgrind outputs. Counts remain separate. The 36 CSV rows contain 27 native
and 9 first-byte distribution groups.

The first attempt stopped before fixture creation or timing because
`callgrind_annotate --version` returned 255. Its original driver, stderr, and logs
are preserved. The annotator now has an exact content hash and version UNKNOWN.
The raw Callgrind creator identifies version 3.27.1, which does not establish the
annotator's own version. The corrected fresh profile passes.

The evidence audit checks independent creation-manifest reference reconstruction,
component-wise DFS byte order, reference hashes/counts, all output receipts,
JSONL samples, distributions, user/system sums, phase inventories, tool/source
identities, and Callgrind raw/annotation hashes. A coherently doubled CPU record
is rejected against the independent GNU time fields. Successful scratch output
is reused, so every old raw capture cannot be revalidated retrospectively.
Runtime validators check every actual output; fd uses a multiset comparison.

All heavy work is sequential under systemd, limited to 3 GiB memory and no swap,
with nice 15, core limit zero, at least 5 GiB disk and million-file inode preflight.
No cache drop, affinity change, install, publication, commit, push, dependency
edit, or security change occurred. Only newly owned temporary fixtures were
removed. Shared-host pressure snapshots do not establish causal isolation,
controlled cold caches, full residency, or a total RSS cap.

All 136 frozen production inputs, 102 qualification inputs, HEAD, and the archived
c-utils pin remain unchanged. Same-family read-only review found no material
phase-boundary defect and supports stopping without a candidate. It did not
independently rehash evidence or run tests. Only the private diagnostic probe
was compiled; no new application acceptance is claimed.

Portable local artifacts:

- [Index and audit](../bench/results/collection-cost.json)
- [Compressed measurements, manifests, sources, and call graphs](../bench/results/collection-cost-data.json.gz)
- [Lossless typed distributions](../bench/results/collection-cost-summary.csv)

Evidence replay without rebuilding million-file fixtures:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; python3 -I build/collection-cost/analyze.py && python3 -I build/collection-cost/final_check.py'
```

The next separate question is the full bounded-radix ordering cost, rather than
another prefix-fallback variant. No sorter change or continuation work has started.
