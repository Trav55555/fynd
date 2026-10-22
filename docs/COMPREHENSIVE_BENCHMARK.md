# Comprehensive benchmark

Historical versions, before [qualified unordered streaming](STREAMING.md).
Its scoped warm million-file wins do not replace or pool the broader data below.

This matrix measures the initial coarse walker. The later
[performance pass](PERFORMANCE_PASS.md) changes output transport and ignore
rejection; its paired results are separate. The historical data below is unchanged.

The broader result is mixed. Fynd has low startup cost and competitive small or
flat searches. Its ordered subtree workers help considerably, but fd remains
faster on several large, output-rich, skewed, package-layout, and ignore-heavy
workloads. The earlier small branching-tree win does not establish general parity.

## Scope and evidence

Completed two separate runs covering 30 fixture trees and 107 workload/sink
combinations, with nine rotated repetitions per configuration:

| Run | Workloads | Configuration prechecks | Timed outputs | Validated warmups |
| --- | ---: | ---: | ---: | ---: |
| Primary | 86 | 1,436 | 12,924 | 774 |
| Matched controls | 21 | 342 | 3,078 | 189 |
| Total | 107 | 1,778 | 16,002 | 963 |

Each run compared fynd and fd at 1, 2, 4, 8, 12, 16, and 32 traversal workers,
plus fd auto and GNU find. The primary run also compared the previous qualified
fynd at j1/j8 on 30 workloads. fd j1 is one traversal worker, not one total thread.
Serial remains fynd's default.

The exact qualified binary came from
`build/coarse-traversal/prototype/build/verification/run.XXAMxe`. The previous
binary came from `build/verification/run.cjDJRE`. Application sources, binary
hashes, and the c-utils pin remained unchanged. No production optimization,
dependency change, installation, project commit, or publication occurred.

Host: Ryzen 5 7600, 12 available logical CPUs, Linux, NVMe ext4 and tmpfs.
Tools: fynd 0.1.0 unreleased, fd 10.3.0, GNU find 4.10.0. GNU time identifies
its build version as UNKNOWN; its executable hash is retained. Both fynd
binaries identify GCC 15.2.0 in their ELF metadata.

Results and complete matrices:

- [All 1,778 summary rows, including p10/p90, CPU, and RSS](../bench/results/comprehensive-summary.csv).
- [Portable index, tool provenance, and manifest references](../bench/results/comprehensive.json).
- [Compressed samples, argv, measured drivers, source hashes, and audit receipt](../bench/results/comprehensive-data.json.gz).
- Compressed creation manifests are alongside those results. Expected output
  references and detailed logs remain in the local raw directories below.

## Primary all-file results

Median elapsed milliseconds. These select regular files, include hidden names,
bypass ignores, and use NUL records. They compare common selections, not each
tool's complete default command behavior. File output includes startup, GNU
time, and writing the result file.

| Dataset | Files | Fynd j1 | Fynd j8 | fd auto | GNU find |
| --- | ---: | ---: | ---: | ---: | ---: |
| Empty directory | 0 | 1.28 | 1.52 | 3.79 | 1.66 |
| Flat 10k | 10,000 | 5.62 | 5.87 | 8.29 | 6.84 |
| Flat 100k | 100,000 | 43.26 | 43.27 | 43.69 | 78.89 |
| Flat 1m | 1,000,000 | 531.82 | 522.44 | 398.90 | 844.56 |
| Deep, 96 levels | 3,072 | 5.02 | 5.38 | 8.83 | 5.84 |
| Balanced, 9,331 directories | 298,592 | 159.85 | 92.82 | 46.68 | 193.27 |
| Manydirs, 55,987 directories | 223,948 | 609.59 | 328.78 | 111.39 | 613.15 |
| Single-child prefix then fork | 111,972 | 122.38 | 61.09 | 32.95 | 139.19 |
| One large flat branch | 100,056 | 42.74 | 42.23 | 43.77 | 72.46 |
| Long-name, output-heavy | 48,000 | 63.09 | 18.33 | 21.57 | 61.25 |
| Shared 200-byte name prefixes | 20,000 | 28.21 | 28.78 | 25.14 | 31.72 |
| Captured Python packages | 19,016 | 38.59 | 23.58 | 12.40 | 38.83 |
| Captured Node packages | 2,104 | 3.25 | 3.16 | 5.41 | 4.05 |

The captured package trees preserve paths and node kinds, not contents, sizes,
executable bits, or live-workspace activity. Four additional immutable tracked
Git layouts cover c-utils, cgrep, the checker toolchain, and pi-web-access.
Their small scans largely measure startup. They are not ignore-enabled live
repository benchmarks.

### Output volume changes the result

On the same large manydirs tree:

| Selection and sink | Fynd j8 | fd auto |
| --- | ---: | ---: |
| All files, regular output file | 328.78 ms | 111.39 ms |
| All files, captured pipe | 240.62 ms | 106.22 ms |
| No matches, regular output file | 93.49 ms | 95.19 ms |

Fynd j8 used about 1.44 logical-core equivalents for the all-file case, versus
4.90 for no matches. Total CPU was similar, about 470 ms in both. fd auto used
about 6.88 and 7.19 respectively. This makes ordered output/backpressure a useful
next investigation; it is not a profile attributing every stalled interval.

The output-heavy pipe case was effectively tied: fynd j8 18.13 ms, fd auto
18.00 ms. Its regular-file median advantage should not be presented as a
universal pipeline win. Neither sink discards output to `/dev/null`; the pipe
includes Python draining/capture, with validation after timing.

### More workers are not automatically better

Selected all-file medians:

| Dataset | Fynd j1 | j2 | j4 | j8 | j12 | j16 | j32 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Empty | 1.28 | 1.32 | 1.49 | 1.52 | 1.69 | 1.87 | 2.32 |
| Flat 100k | 43.26 | 44.36 | 43.54 | 43.27 | 44.00 | 43.94 | 44.40 |
| Balanced | 159.85 | 93.20 | 90.72 | 92.82 | 93.53 | 97.98 | 92.87 |
| Python packages | 38.59 | 32.38 | 27.52 | 23.58 | 22.01 | 20.75 | 20.28 |

Complete findings include oversubscription and every fd thread count in the CSV.
Do not choose a universal thread default from the fastest observed cell.

## Matched controls

These are separate measurements, not samples pooled with the primary run.

### Same selected tree, more ignore rules

All three fixtures have identical relative paths/kinds and select 6,281 files.
The variable is the number of additional miss-only `.ignore` rules; fixed Git,
plain, global, exclusion, and nested rules remain present in every fixture.

| Additional rules | Fynd j1 | Fynd j8 | fd auto | Find with equivalent predicates |
| --- | ---: | ---: | ---: | ---: |
| 0 | 6.00 ms | 2.98 ms | 7.32 ms | 5.41 ms |
| 64 | 27.35 ms | 8.80 ms | 8.84 ms | 5.78 ms |
| 512 | 176.41 ms | 47.17 ms | 15.81 ms | 5.68 ms |

At 512 extra rules, fynd j8 spent about 277 CPU ms versus fd's 30.5 CPU ms.
This is an ignore-processing efficiency gap, not simply insufficient threads.
Find does not parse ignore files: its command encodes the same known pruning
and selection. Its row is a traversal/filter reference, not a native ignore engine.

The primary `ignore-prune` fixture has fewer kept files than `ignore64/512`.
It measures pruning, not zero-to-512 rule scaling. Use the matched controls above.

### Recursive skew

One branch contains 111,972 files in a recursive tree; seven siblings are tiny.
For all 111,986 files, fynd j8 took 147.18 ms versus fd auto's 41.07 ms.
For no matches, the comparison was 87.45 versus 23.97 ms. Fynd stayed near one
logical CPU in the quiet case; fd auto used about 6.58. Whole-subtree tasks do
not repartition their dominant branch. The primary flat-skew fixture alone
would have missed this limitation.

### Matched ext4 and tmpfs

Both trees have identical relative paths/kinds: 37,324 files in 9,331 directories.

| Filesystem | Fynd j1 | Fynd j8 | fd auto | GNU find |
| --- | ---: | ---: | ---: | ---: |
| NVMe ext4 | 81.76 ms | 19.58 ms | 23.34 ms | 105.33 ms |
| tmpfs | 71.25 ms | 16.61 ms | 23.93 ms | 94.77 ms |

The primary `manydirs` and `ram-manydirs` have different depths. Do not divide
those timings to infer a filesystem speedup. The matched pair above isolates
that comparison more closely, without claiming controlled cold-cache behavior.

## Variability, CPU, and memory

The host was not isolated. Primary load averages rose from about 1.38 to 5.40;
the matched run started near 3.98 and ended near 5.41. Processes used nice 15.
The CSV includes all nine samples' summaries, including p10/p90 and extrema.
Small differences and isolated best cells are not reliable universal rankings.

For example, large manydirs fynd j8 ranged from 224 to 465 ms, with p10/p90
247/386 ms. fd auto's p10/p90 was 101/126 ms. The large gap persists despite
noise. Output-heavy regular-file results had much wider tails than their medians.

Some serial wall medians regressed against the previous binary. CPU did not
show the same regression: large manydirs was about 462 versus 464 CPU ms;
recursive skew was about 101 CPU ms for both. Elapsed time alone cannot assign
those differences to new code rather than waiting or descheduling. These need
an isolated follow-up before claiming a code-level serial regression.

The scope's 3 GiB limit includes the Python driver, applications, and charged
cache. Observed driver peak RSS was about 1 GiB. Scope memory reached its
cap; sampled later intervals showed no additional memory-pressure accumulation,
OOM, or CPU quota throttling. Monitoring was periodic, not continuous. Each
group had a validated serial warmup, but full cache residency is not guaranteed.

Application RSS, rather than total scope memory, is recorded by GNU time.
Selected primary median RSS, MiB:

| Dataset | Fynd j1 | Fynd j8 | fd auto | GNU find |
| --- | ---: | ---: | ---: | ---: |
| Flat 1m | 62.81 | 62.94 | 12.72 | 31.48 |
| Manydirs | 1.69 | 7.69 | 10.91 | 2.54 |
| Output-heavy | 3.31 | 22.69 | 22.40 | 5.29 |
| Python packages | 1.69 | 2.44 | 11.03 | 2.73 |

Fynd's complete sorted listings cost memory on extremely wide directories;
its smaller RSS on other shapes is not a universal memory advantage.
CPU uses child-resource deltas; it includes the small timer launcher cost.
The independent coarse GNU-time CPU fields agreed within 20.91 ms across every
sample, within truncation/launcher allowance. A coherently doubled heavy CPU
record failed the independent timer crosscheck in both runs. CPU/elapsed is logical-core equivalence,
not physical-core utilization; component medians need not sum to the total median.

## Method, reproducibility, and limits

Within each run, all configurations passed before any timing. Every measured output then matched
an independent creation-manifest multiset; every fynd/previous-fynd output also
matched independent unsigned-byte sorted DFS order. Duplicate roots, raw names,
links, FIFOs, hidden policy, ASCII case, extension unions, and depth limits are
covered. Timings include process startup and output, not just directory walking.

Two rejected preflights exposed fd CLI differences. Native `fd -e c` includes
`.C` even with `--case-sensitive`; suffix cases therefore use byte-regex filters
and `--and`. Directory-only fd output adds slashes; `--format '{}'` requests
unadorned paths. These flags are timed. Mismatches were not normalized afterward.

The primary run used the exact embedded measured driver. A read-only review
then identified two failure-artifact gaps: initialization errors and bad pipe
warmups. Those paths were repaired and tested before the matched run. The
success-path timing/accounting code did not change. Both driver versions are
retained in the portable JSON; no different-model-family review is claimed.

`make bench-test` passed 34 fake-tool/fixture/runner tests. `make bench-full`
provides the capped entry point; see [the procedure](../bench/README.md).
Detailed local runs:

- `build/comprehensive/run-1hvreac0`, primary, exit 0.
- `build/comprehensive/supplement-k4c3_vvp`, matched controls, exit 0.
- `build/comprehensive/analyze.py`, source/binary/manifest/sample audit.

No controlled cold-cache, slow/network filesystem, native Windows, permission
failure timing, or live repository-content result is claimed. No security or
scheduler settings were changed. Existing historical runs and failed preflight
evidence remain intact; only newly owned temporary fixtures were cleaned.
