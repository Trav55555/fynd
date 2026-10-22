# Sort optimization results

Historical sorter result. [Qualified streaming](STREAMING.md) now removes this
sorter and its directory-wide vector from the default implementation. Its tests
and source versions remain preserved as evidence for the measurements below.

The retained sorter reduces flat-directory scan time. It does not solve the
many-directory scaling gap, and very long shared filename prefixes can be
slower. Serial remains the default.

[Samples, source, binary hashes, and drivers](../bench/results/sort-optimization.json)
identify the measured code. Final raw runs are
`build/sort-experiment/run-k8o8_d1w` and `names-kj5fp_1d`.

## Retained algorithm

`src/listing.inc` uses insertion sort for up to 16 entries and unsigned-byte
three-way radix partitioning for larger ranges. A median-of-three byte selects
the partition. Equal groups advance one byte only when that byte is nonzero.
After 32 partition levels, the sorter falls back to full-name `qsort`.

The budget bounds radix recursion and repeated prefix-partition work. It does
not replace the C library's fallback complexity contract. Sorting swaps whole
`WalkEntry` records, preserves their payloads, and adds no explicit allocation.
It still completes before DONE publication. Output order, ignore rules,
partial-listing discard, job limits, errors, and the public API are unchanged.
No dependency revision or library API changed.

Three prototypes compared the existing qsort algorithm, tiny insertion/qsort,
and the bounded radix hybrid. The radix hybrid showed the clearest flat-directory
gain. The final comparison below uses the original accepted B1HO3G executable,
not the prototype's uniform qsort wrapper, against the newly qualified binary.
The tiny-only candidate remains in the samples as a control.

## Native measurements

Nine repetitions covered four tree shapes, all-files and selective queries,
and one/four/eight workers, with automatic fd as a reference. All 80 prechecks
and 720 timed outputs matched independent creation manifests. Exact ordered
comparisons covered all 72 fynd prechecks and 648 timed fynd outputs.

Median all-files elapsed time, milliseconds:

| Shape | Old j1 | New j1 | Old j4 | New j4 | Old j8 | New j8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Wide, 100,000 files | 50.95 | 42.47 | 50.88 | 43.89 | 52.03 | 43.49 |
| Deep, 96 levels | 4.49 | 4.57 | 5.50 | 5.41 | 6.01 | 5.68 |
| Mixed, 49,760 files | 26.66 | 26.51 | 27.41 | 27.87 | 25.31 | 24.72 |
| Many directories, 9,331 | 64.31 | 64.14 | 67.10 | 67.12 | 72.87 | 73.09 |

The flat j1 median used about 17% less elapsed time. Other shapes did not show a
consistent large improvement. Automatic fd measured 40.77 ms on the flat fixture
and 21.20 ms on the many-directory fixture. These configurations do not promise
the same ordering contract.

Additional serial checks used 20,000 files per distribution and seven
repetitions. All 18 prechecks and 126 timed outputs passed creation-manifest
and exact byte-order comparisons, including filenames beginning with bytes
128 through 255.

| Filename distribution | Old | New |
| --- | ---: | ---: |
| Random ASCII prefix | 10.91 ms | 9.70 ms |
| Shared 220-byte prefix | 26.66 ms | 27.52 ms |
| Raw high-byte prefix | 11.10 ms | 9.54 ms |

The long-prefix median regressed about 3%. The bounded prefix work prevents
unbounded radix descent, but does not make every distribution faster. Retention
accepts that measured tradeoff rather than claiming a universal speedup.

The tests used warm ext4 on NVMe, Ryzen 5 7600 with 12 logical CPUs available,
ignores bypassed, hidden entries included, regular files, and NUL output. Startup,
GNU time, and output-file I/O count toward elapsed time. Invocations ran
sequentially under 3 GiB, no swap, nice 15, and no core dumps. Background activity
was uncontrolled; the primary run's one-minute load was about 2.19 to 2.17.
No cache dropping, tool installation, or system security changes occurred.
These are synthetic warm-cache results, not statistical-significance or
cold-storage claims. Earlier prototype measurements are retained locally.

## Verification

`make verify` passed with receipt `build/verification/run.cjDJRE`, exit 0.
All required compiler, analyzer, sanitizer, TSan, optimized/i386/WASI, Valgrind,
coverage, four bounded semantics cases, and four fuzz targets passed. The
unchanged optional authority, regions, and CBMC checks explicitly skipped.

The run passed 6,656 CLI checks and all four C executables. New direct sort tests
cover zero/one entries, cutoff boundaries, duplicates, raw bytes, reverse and
organ-pipe inputs, 4,096-byte shared prefixes, exact payload preservation, and
an independent byte/length oracle. Invalid sentinel identities in the fuzz
builder expose missing writes. An initial analyzer finding in oracle construction
was resolved without suppressions or blanket zero initialization.

All 18 compiled behavioral controls were rejected. Four new controls drop equal
partitions, remove the partition budget, allow unbounded insertion sort, and
flip byte order. Operation assertions reject excessive prefix or quadratic
insertion work without using timing as the oracle. This is a named sensitivity
set, not a code-wide mutation score.

Line coverage was 97.72% for `src/fynd.c`, 95.78% for `src/ignore.inc`, and 97.44%
for `src/listing.inc`, with the unchanged 90% per-module floor. See
[verification evidence](VERIFICATION.md) for receipts and limits. No whole-program
filesystem proof or new native-platform qualification is claimed.

## Reproduce locally

The data embeds measured source, native commands, and driver sources. The
accepted dependency revision and benchmark-core hashes identify shared inputs.
Local reproduction requires the recorded acceptance snapshots and their native
binaries, or rebuilding those source versions against the immutable pin.

Restore the replay preparation script, sorter snippet, test template, and native
driver under `build/sort-experiment/` if absent. Restore the embedded fixture
driver under `build/benchmark-request/driver.py`. Use a new owned output directory:

```sh
python3 -I build/sort-experiment/prepare.py --output build/sort-experiment/replay
systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c '
    ulimit -c 0
    dep="$PWD/build/verification/run.B1HO3G/source/c-utils"
    for variant in before tiny radix; do
      timeout 240 make -j2 -C "build/sort-experiment/replay/$variant" CUTILS="$dep" all test || exit $?
    done
    exec python3 -I build/sort-experiment/experiment.py \
      --inputs build/sort-experiment/replay \
      --candidate build/verification/run.cjDJRE/source/fynd/build/native/fynd
  '
```

For the filename-distribution run, use the same capped launch with
`name_shapes.py --inputs build/sort-experiment/replay --candidate` followed by
the qualified binary path. The drivers validate every output and recheck binary
hashes after measurement. Owned temporary fixtures are removed on exit.
