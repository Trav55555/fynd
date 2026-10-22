# Full streaming benchmark

The qualified streaming implementation completes the full existing benchmark:
107 workloads, 30 fixture trees, nine repetitions, 1,778 prechecks, 16,002 timed
outputs, and 963 warmups. All output checks pass. This benchmark changes no
application code, dependency, default, or qualification requirement.

## Fixed configurations against fd-auto

A win below means a lower point median, not a statistically established win.
The workload counts are not a weighted estimate of everyday searches.

| Run | Workloads | Default fynd-j1 lower | Fixed fynd-j8 lower |
| --- | --- | --- | --- |
| Primary | 86 | 56 | 81 |
| Matched controls | 21 | 8 | 18 |

The primary j8 losses are `balanced-all-file` and all four Python package-layout
queries. Balanced all-file is only 0.8% slower. The package queries are about
61-71% slower than fd-auto. All three dominant-branch skew queries lose in the
matched run, taking about 2.8-3.2 times fd-auto's elapsed time. More threads do
not close those package or dominant-branch gaps in this run.

Serial million-file output takes 300.05 ms against fd-auto's 394.76 ms,
about 24% less elapsed time. This full matrix has a 100k-file pipe workload,
not a million-file pipe workload. Its timings are completion measurements,
not first-byte latency. Earlier million-file pipe and first-byte evidence
remains in the separate [streaming qualification report](STREAMING.md).

## Primary medians

All values are elapsed milliseconds. The two fynd columns use fixed thread
settings, not the fastest setting selected separately for each row. The full
CSV includes every query, sink, tool, and thread setting, with p10, p90,
minimum, maximum, CPU, RSS, and context-switch distributions.

| run | case | fynd-j1 | fynd-j8 | fd-auto | find |
| --- | --- | --- | --- | --- | --- |
| primary | empty-all-file | 1.31 | 1.60 | 3.78 | 1.80 |
| primary | tiny-all-file | 1.37 | 1.57 | 4.35 | 1.80 |
| primary | flat100k-all-file | 32.62 | 33.63 | 44.60 | 76.68 |
| primary | flat1m-all-file | 300.05 | 305.61 | 394.76 | 621.44 |
| primary | flat100k-all-pipe | 31.19 | 31.35 | 44.29 | 74.32 |
| primary | deep-all-file | 4.87 | 4.71 | 8.76 | 5.62 |
| primary | balanced-all-file | 138.00 | 44.54 | 44.18 | 191.34 |
| primary | manydirs-all-file | 384.07 | 101.62 | 107.93 | 603.60 |
| primary | prefix-all-file | 95.89 | 29.39 | 33.38 | 136.94 |
| primary | skew-all-file | 33.03 | 31.80 | 44.68 | 74.39 |
| primary | output-all-file | 46.91 | 13.94 | 24.71 | 61.27 |
| primary | sharedprefix-all-file | 20.81 | 20.59 | 24.57 | 33.39 |
| primary | packages-all-file | 25.46 | 20.85 | 12.21 | 38.63 |
| primary | node-packages-all-file | 3.18 | 3.17 | 5.32 | 4.18 |
| primary | ignore64-all-file | 7.93 | 4.16 | 8.48 | 5.81 |
| primary | ignore512-all-file | 20.94 | 7.95 | 16.14 | 5.59 |

## Matched-control medians

These are separate distributions, not additional repetitions of the primary
workloads. The two storage trees have matching relative nodes and depth.
The three rule-count trees have matching kept-tree nodes, unlike the primary
pruning fixture. Root labels and ignore-file contents still differ. GNU find
uses explicit predicates for this controlled ignore SPEC, not a Git ignore
implementation.

| run | case | fynd-j1 | fynd-j8 | fd-auto | find |
| --- | --- | --- | --- | --- | --- |
| matched | skew-branch-all-file | 90.80 | 84.08 | 29.77 | 130.45 |
| matched | store-ext4-all-file | 68.82 | 17.44 | 23.56 | 105.37 |
| matched | store-ramx-all-file | 58.35 | 14.77 | 22.64 | 93.94 |
| matched | matched-ignore00-all-file | 6.22 | 3.50 | 7.66 | 5.82 |
| matched | matched-ignore64-all-file | 8.46 | 4.24 | 9.08 | 5.91 |
| matched | matched-ignore512-all-file | 21.00 | 7.22 | 16.05 | 5.63 |

## Thread scaling

Elapsed milliseconds for the same all-file queries. Serial remains the default.
A flat directory gains nothing from extra workers here. Balanced and many-directory
trees benefit, while package layout and dominant-branch skew retain large gaps.

| run | case | fynd-j1 | fynd-j2 | fynd-j4 | fynd-j8 | fynd-j12 | fynd-j16 | fynd-j32 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| primary | flat1m-all-file | 300.05 | 307.13 | 305.89 | 305.61 | 305.36 | 308.81 | 310.27 |
| primary | balanced-all-file | 138.00 | 75.95 | 57.28 | 44.54 | 37.01 | 35.43 | 36.44 |
| primary | manydirs-all-file | 384.07 | 197.60 | 152.07 | 101.62 | 85.90 | 87.13 | 92.27 |
| primary | packages-all-file | 25.46 | 27.20 | 23.69 | 20.85 | 20.57 | 20.02 | 19.93 |
| matched | skew-branch-all-file | 90.80 | 85.99 | 85.48 | 84.08 | 85.81 | 85.27 | 85.47 |

## Method and limits

- Primary `run-xjobxag2` covers 24 trees and 86 workloads: 1,436 prechecks,
  12,924 timed samples, and 774 warmups. Matched `supplement-gwq3qhdv` covers
  six trees and 21 workloads: 342 prechecks, 3,078 samples, and 189 warmups.
- Both runs use fynd and fd at 1, 2, 4, 8, 12, 16, and 32 configured threads,
  plus fd automatic threading and GNU find. Selected cases also compare the
  prior qualified sorted fynd at j1 and j8. fd-j1 is not one total process thread.
- Case and configuration order rotates between repetitions. Nine repetitions
  do not balance every position across all 16 or 18 configurations.
- All prechecks precede timed sampling. Each workload has a serial fynd warmup
  before its rotated configuration group. File and pipe output are real captures,
  never `/dev/null`. Runtime validation checks NUL records, multiplicity, and
  root groups. The old sorted baseline also requires exact ordered bytes.
- The independent creation manifests define selection. A separate component-tuple
  DFS and controlled-ignore SPEC reconstructs reference bytes during audit.
  Canonical actual-output receipts come from runtime validation. Successful raw
  captures are reused, so the archive cannot retrospectively rehash every raw
  output or independently re-prove all runtime root-group checks.
- Six repository and package trees replay the exact historical path/type manifests
  from `build/comprehensive/run-1hvreac0`. Files have empty contents and links have
  placeholder targets. These are metadata-layout benchmarks, not fresh live trees,
  source-content copies, or package-content fidelity tests.
- fd is 10.3.0 and GNU find is 4.10.0. GNU time reports version `UNKNOWN`.
  Exact tool hashes and version captures are retained before and after both runs.
- The application is the selected [streaming implementation](STREAMING.md),
  qualified by `build/streaming/qualification/build/verification/run.vbgYhn`.
  All 102 qualified inputs match before and after. The dependency is the extracted
  immutable c-utils revision `eafa795805fa997e2db91f9182c4e0f86fa32fb4`,
  not the mutable sibling checkout. HEAD remains
  `0c7f8a3a72525d51e04dbb124e6eee4c593cfce3`.
- Native CPU totals agree with independent GNU time within 20.89 ms in primary
  samples and 20.50 ms in matched samples. Coherently doubled CPU records fail
  that check. These totals are not attribution to specific C functions.

Both runs execute sequentially under `MemoryMax=3G`, `MemorySwapMax=0`, nice 15,
and zero core limits. A two-second observer records resources with its own
small overhead. Primary scope peak reaches 3,072 MiB and records 124 memory-max
events. It has no OOM kills or swap use. That limit-related pressure prevents
any claim of unrestricted cache residency or memory-pressure-free timing.
The observed Python driver peak is 1,077.16 MiB. The matched scope peaks at
1,046.58 MiB, with a 195.61 MiB driver peak and no memory-max events.
These scope peaks include the driver, children, and charged cache. They are
not fynd RSS. Serial million-file fynd has a 1.69 MiB median per-process peak RSS.

Neither run records CPU quota throttling. `io.stat` is unavailable at the scope
leaf, so there are no per-scope I/O byte totals. There was no cache dropping,
affinity or security change, dependency update, or limit increase.
These are warmed shared-host measurements, not isolated-host or controlled-cold
results. Load averages rise during the run. No universal fd superiority,
network-storage result, native Windows result, or new first-byte claim follows.

## Failed attempt and verification

The first attempt, `run-p9khtakf`, stops during fixture creation because the
observer treats missing `io.stat` as fatal. It has no prechecks, timed samples,
or warmups. The private wrapper then records unavailable optional telemetry,
while OOM and swap monitoring stay mandatory. No application bytes change.
The original source, traceback, exit status, and failed receipt remain retained.
Owned successful fixture directories are absent after cleanup.

Preflight passes 34 benchmark tests and materializes all six captured layouts.
The final audit verifies manifests, reference bytes, actual-output journals,
all counts and rotated schedules, source/tool identity, CPU accounting, resource
records, and all 1,778 separate CSV distributions. Negative controls reject
changed canonical hashes, qualification pointers, source hashes, archive bytes,
CSV values, and coherently forged CPU totals. These are benchmark evidence
checks, not a replacement qualification campaign or a whole-program proof.

## Retained evidence

- [Index](../bench/results/streaming-full.json)
- [Portable data](../bench/results/streaming-full-data.json.gz)
- [Complete summary CSV](../bench/results/streaming-full-summary.csv)
- Local receipt: `build/streaming-full/receipt.md`
- Analyzer: `build/streaming-full/analyze.py`
- Replay check: `build/streaming-full/final_check.py`

The data archive retains measured driver sources, fixture manifests, independent
references, output-validation journals, resource samples, tool versions, and
qualification/source identities. Primary and matched distributions remain separate.
The checker and its hash-bound generic CPU helper are required for replay.
No million-file fixture recreation is needed.

```sh
cd /home/travers/projects/fynd
ulimit -c 0
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/streaming-full/final_check.py'
```

`--portable-only` skips original run directories, binary files, and qualification
trees. It still requires the trusted analysis/checker/helper scripts and documentation.
Nothing was committed, pushed, installed, or published.
