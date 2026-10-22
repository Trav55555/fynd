# Listing latency after the memory improvement

Historical rejected sorter experiment. [Current unordered streaming](STREAMING.md)
is a separate qualified implementation, not another prefix-sort variant.

The prefix-aware sort candidate is **not retained**. It improves shared-prefix
first-byte medians by 6.39% and 5.39%, below the declared 15% gate. Native elapsed
gains on that primary case are 5.55% and 4.08%. Plain million-file first-byte
latency is essentially unchanged. Several controls also flag repeated elapsed
regressions. No promotion occurred, and full qualification was not run.

Production remains the [qualified record-arena implementation](LISTING_QUALIFICATION.md),
`run.Nano7T`. Its roughly 63 to 39 MiB memory improvement remains retained.
The experiment starts from that version, without rejected continuation code.

## Current-build profile

`build/listing-latency/profile-srbwa5wz` measures the retained executable, not
the earlier pre-storage-change binary. Its separate j1 phase probe records:

- Plain million-file collection CPU around 293 to 296 ms and sort CPU around
  131 to 135 ms. No comparison-sort fallback occurs on these names.
- For 20,000 names with a 200-byte shared prefix, collection takes around
  16.5 ms CPU and sorting around 6.4 to 7.6 ms CPU. Two fallback groups cover
  all entries. Their maximum known prefix depth is 32.

These instrumented observations do not establish native CPU fractions. Inventory
work and clocks perturb the probe; collection-through-sort is not CLI latency.
The probe supports only j1 and one completed flat listing.

A census correction matters here. The design sketch initially described all
fallback entries as depth 32. The recorded maximum is 32, while 2,858 hidden
names reach depth 31. The weighted census is 637,142 entry-prefix bytes, not
640,000. The original sketch is preserved. The first audit rejected the mistaken
expectation before it was corrected; neither data nor candidate code changed.

In separate native captured-pipe observations, the retained j1 executable first
produces consumer-observed bytes around 424 ms on plain million-file searches
and 24 ms on shared-prefix searches. These profile distributions are separate
from the later matched waves below.

## First byte is not completion

The new observer starts a process, waits for stdout readability, and reads one
byte with `os.read`. It then drains the remaining bytes with `communicate`.
A buffered `read(1)` could prefetch bytes that `communicate` would miss; the raw
read avoids that loss. Every captured output is validated after timing.

The deadline covers both first-byte waiting and the remaining output. Nonzero
status, any stderr, or timeout fails the observation. Timeout cleanup kills the
process group even when its leader has exited. Empty stdout has no first-byte
time. Twelve fake-tool tests cover exact binary bytes, large multi-chunk output,
empty output, delayed first bytes and tails, errors, and descendant cleanup.

The timestamp records a consumer read. It includes launch, stdio buffering,
observer scheduling, and pipe effects, not the exact kernel write time.
Completion excludes subsequent artifact writes and validation. These captures
have no GNU time wrapper or CPU/RSS measurements. Standard native file/pipe
samples retain their separate CPU timer and RSS measurements.

## One isolated comparison change

Only the private candidate changes sorting. Before the existing qsort fallback,
it finds a bounded shared prefix using 64-byte comparisons and a short scalar
tail. It inspects at most 2,048 additional prefix bytes per name. The original
32-partition recursion budget, insertion threshold, and existing operation-count
limit remain unchanged.

The candidate temporarily shifts private name pointers and lengths to suffix
views, sorts whole records with the existing comparator, then restores every
pointer and length. It does not alter name bytes, allocate storage, move arena
owners, or introduce shared comparator state. Sorting still completes before
listing publication. Cancellation still does not interrupt synchronous sorting.

A compile-valid old-code test passes independent ordering, permutation, payload,
name-immutability, and cleanup checks, then aborts on its fixed comparison budget.
It counts 90,192 logical compared-byte pairs against a budget of 8,224. The
candidate counts 2,387. These counts describe comparator string arguments, not
vectorized libc reads or native CPU work. The old assertion exit is 134.

Additional fixtures cover depth 0 and 32, chunk boundaries, the 2,048-byte cap,
shorter names, shrinking prefixes, duplicates, high bytes, exact pointer/length
restoration, and immutable name bytes. Long-prefix fuzz and semantics cases were
added, but their full fuzz/interpreter campaigns were not run for this rejected
candidate.

## Matched native results

Each wave runs 22 cases, nine configurations, and nine repetitions. Configurations
are baseline/candidate j1/4/8/16 plus fd-auto. All configurations occupy every
position once per case. The second wave reverses the rotated configuration order.
Empty/tiny, 1,024/1,025 thresholds, flat 10k/100k/1m, deep, branching, quiet,
shared-prefix, file, and captured-pipe controls remain included.

The predeclared gate requires at least 15% improvement on the same primary case
and metric in both waves. Primary cases are plain million-file and shared-prefix
all-results pipes at j1. It also requires no repeated elapsed regression above
5% on controls and no million-file RSS regression above 5%.

Medians below are baseline to candidate in milliseconds. Native completion and
observed first bytes are different sample sets.

| Case and metric, j1 | Wave 1 | Wave 2 |
| --- | ---: | ---: |
| Shared-prefix native completion | 24.95 to 23.56 | 25.34 to 24.31 |
| Shared-prefix observed first byte | 21.46 to 20.09 | 21.48 to 20.32 |
| Million-file native completion | 451.18 to 459.53 | 456.96 to 468.58 |
| Million-file observed first byte | 370.99 to 374.29 | 362.07 to 363.22 |

Wave 1 is `run-nqifo4ea`; wave 2 is `run-eck_7202`. Candidate million-file
all-results j1 RSS is about 38.8 MiB in both waves. The memory control passes,
but the latency requirement does not.

Repeated control flags, based on ratios of point medians:

| Control | Threads | Wave 1 slower | Wave 2 slower |
| --- | ---: | ---: | ---: |
| Flat10k all-results pipe | 16 | 5.30% | 8.08% |
| 1,025-entry all-results file | 8 | 13.26% | 6.05% |
| 1,025-entry all-results file | 16 | 6.01% | 5.38% |
| Tiny quiet file | 8 | 11.92% | 5.02% |

These flags do not establish intrinsic slowdowns or explain host noise. The
benefit gate independently fails, so no repeat-until-favorable follow-up or
speculative patch was attempted.

## Verification boundary and evidence

The candidate passes eight C executables, 508 CLI checks, 140 ignore checks,
34 benchmark tests, and targeted fast checks. Clang ASan/UBSan passes the prefix
and existing sort tests. The unchanged existing sort-work limit also passes.
No new mutation campaign, per-module coverage measurement, or full qualification
is claimed. The current retained production qualification remains unchanged.

The first preflight passed all 12 observer tests but used a wrong relative make
target for the private probe. Building its declared default target fixed the
invocation. The first narrow run passed behavior but rejected a new test's
zero-fill memset; element initialization fixed that lint failure. Both failed
logs are retained. Neither is counted as a behavioral mutant kill.

Totals remain separated: 423 prechecks, 3,699 standard native timings, 441
warmups, 351 observed first-byte captures, and 12 diagnostic outputs. The CSV
contains 468 typed distribution rows: 423 native groups and 45 first-byte groups.

All heavy work runs sequentially under systemd with a 3 GiB memory limit, no
swap, nice 15, core limit zero, at least 5 GiB disk preflight, and inode checks.
Periodic wave counters show no new memory-max, OOM, or CPU-throttling events.
Both waves share one scope, so the second wave's memory-peak counter includes
history. These observations do not establish cache residency or host isolation.

The audit checks frozen sources and tools, JSONL order, balanced rotations,
output counts and hashes, creation manifests, reference bytes, distributions,
CPU consistency against GNU time, archive integrity, and lossless CSV conversion.
It rejects a coherently doubled CPU record. Manifest-based reference rebuilding
uses component-wise DFS order, not global full-path sorting. Successful raw
scratch captures are reused, so retrospective independent revalidation of every
raw output, especially fd order, is unavailable. Runtime validators check every
actual output; fd uses a multiset rather than fynd's exact ordered bytes.

All 133 frozen production inputs, 102 current qualification inputs, HEAD, and
the immutable c-utils pin remain unchanged. No commit, push, install, publication,
dependency edit, security change, cache drop, or affinity change occurred.
Same-family read-only reviews found no blocking defect; they did not independently
rehash raw evidence. Candidate verification is partial, not release acceptance.

Portable local artifacts:

- [Index and gate](../bench/results/listing-latency.json)
- [Compressed data, manifests, versions, and receipts](../bench/results/listing-latency-data.json.gz)
- [Lossless typed distributions](../bench/results/listing-latency-summary.csv)

Private drivers, failed logs, probe, candidate, and old sensor source remain in
`build/listing-latency/`. Evidence replay without recreating million-file trees:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; python3 -I build/listing-latency/analyze.py && python3 -I build/listing-latency/final_check.py'
```

The subsequent [collection-cost attribution](COLLECTION_COST.md) investigates
the retained build and stops without a justified application candidate.
No additional fallback variant or continuation work was started.
