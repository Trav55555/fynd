# Walk optimization results

The changes remove redundant work and reduce queue wakeups. Native gains were
modest, and serial remains the default. This does not close the gap with fd on
branching trees.

[All samples, source variants, binary hashes, and experiment drivers](../bench/results/walk-optimization.json)
are retained. Raw artifacts are under `build/optimize-walk/`, with native samples
in `paired-tdub3wn7`.

## Changes

- `src/fynd.c` no longer constructs a temporary path to decide eligibility when
  `--no-ignore` is active. Visitation still constructs the required display path.
- `src/listing.inc` uses separate work and completion conditions under the same
  mutex. Completion notifications no longer wake idle collectors. Cancellation
  wakes both conditions; startup failure and cleanup cover the added resource.
- `tests/test_parallel.c` adds a deterministic running-cancellation case and
  extends failure injection through the second condition and both worker starts.
  No timing threshold proves concurrency or cancellation.

Sorted DFS, hidden/ignore policy, partial-listing discard, global pending-job
limits, and the public API are unchanged. No library API, dependency revision,
ordering mode, or default thread count changed.

## Native measurements

Before, path-only, and final executables ran in rotated order on the same four
fixtures as [the larger matrix](BENCHMARK_MATRIX.md). Nine repetitions covered
all-files and selective searches at one, four, and eight workers, with automatic
fd as a reference. All 80 prechecks and 720 measured outputs matched independent
creation manifests. Separately, 72 fynd precheck outputs and 648 timed outputs
matched the baseline's exact bytes, including ordering.

Median all-files elapsed time, milliseconds:

| Shape | j1 before | j1 after | j4 before | j4 after | j8 before | j8 after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Wide, 100,000 files | 53.60 | 52.72 | 54.05 | 52.95 | 53.79 | 53.02 |
| Deep, 96 levels | 4.82 | 4.61 | 5.66 | 5.81 | 6.13 | 6.13 |
| Mixed, 49,760 files | 28.06 | 27.11 | 29.34 | 28.16 | 27.72 | 26.11 |
| Many directories, 9,331 | 67.24 | 65.69 | 70.45 | 69.42 | 80.25 | 74.20 |

The JSON includes path-only results, selective queries, every sample, and peak
RSS. The path-only variant isolates path work from the queue change. At eight
workers, the many-directory median moved from 81.12 ms with the path-only change
to 74.20 ms with both changes. fd automatic threading still led the branching
fixtures, at 12.05 ms mixed and 21.98 ms many-directory in this run.

Conditions were warm ext4 on NVMe, AMD Ryzen 5 7600 with 12 logical CPUs
available, ignores bypassed, hidden names included, regular files, and NUL
output. Startup, GNU time, and output-file I/O count toward elapsed time.
Invocations ran sequentially under 3 GiB, no swap, nice 15, and no core dumps.
Background activity was uncontrolled; one-minute load moved from 3.35 to 4.83.
These are descriptive medians, not statistical-significance or cold-storage
claims. Some configurations did not improve.

## Instrumented evidence

Four small single-worker cases covered all-match and no-match queries on an
8,192-file flat directory and a 4,096-file tree with 257 directories. Callgrind
reported 12.5% to 17.2% fewer total instructions after removing redundant paths.
The path-only and final variants had identical instruction counts in these
cases. These are instruction counts, not native CPU-cycle or time reductions.

Three traced repetitions on the small directory tree gave these median futex
syscall counts:

| Workers | Before | After |
| --- | ---: | ---: |
| 1 | 0 | 0 |
| 4 | 2,253 | 1,690 |
| 8 | 3,783 | 2,915 |

Tracing perturbs scheduling. The 23% to 25% reductions support investigating
wakeups; they do not establish a native runtime fraction. Hardware sampling
remains unavailable under the host's perf policy. No security settings changed.

## Qualification and reproduction

Final acceptance passed as `build/verification/run.B1HO3G`; see
[verification evidence](VERIFICATION.md). It includes TSan, the running-cancel
case, all startup failure points, and 14 compiled behavioral defect controls.
An earlier attempt passed deep checks but rejected the new control as a compile
failure. The control now uses a wrong return-value check that compiles and fails
behaviorally; no diagnostic or gate was weakened.

The portable JSON embeds both driver sources and both changed production
modules for each variant. Its binary/source hashes identify the exact inputs.
For a local rerun, preserve the three binaries as
`build/optimize-walk/{before,paths,after}/fynd`, restore the embedded drivers to
`build/optimize-walk/experiment.py` and `build/benchmark-request/driver.py`, and
run the same capped command:

```sh
systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/optimize-walk/experiment.py benchmark'
```

The driver reuses `bench/bench.py`, independently validates every result, records
binary provenance, and checks exact successful fynd ordering. The JSON records
the compiler flags, immutable c-utils revision, and benchmark-core hash. Restore
variant source from that data and rebuild against the recorded pin if the local
binaries are unavailable. No installation or cache dropping is needed.
