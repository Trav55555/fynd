# Larger thread-count benchmark

Fresh measurements of the accepted `-O2 -g` native binary from verification
`run.2Qgzaa`. No application or benchmark-core code changed for this run.
[All samples, commands, hashes, and generator source](../bench/results/large-thread-matrix.json)
are preserved. Raw artifacts: `build/benchmark-request/run-ba3c5mvg`.

## Conditions

- AMD Ryzen 5 7600, 12 logical CPUs available; Linux, ext4 on NVMe.
- Fixtures were created on ext4 under the output directory, not `/tmp` tmpfs.
  Creation and validation warmed the cache. This does not measure cold storage.
- Eight repetitions per configuration/query/workload. All 64 prechecks and
  512 measured outputs passed independent creation-manifest comparisons.
- Benchmark invocations ran one at a time under 3 GiB, no swap, nice 15, and no
  core dumps. Background
  activity was not controlled; one-minute load moved from about 2.79 to 3.24.
- Hidden names included, ignores bypassed, regular files only, NUL output.
  `fd auto` means automatic worker selection only, not all of fd's defaults.
- Configurations rotate within each workload and workloads rotate per repetition.
  Time includes startup, GNU time, and output-file I/O. Peak RSS is GNU time's
  child-process measurement. No timing ratio is a correctness gate.
- fd 10.3.0, GNU find 4.10.0, GNU time reporting `UNKNOWN`. Executable hashes
  distinguish the timer and the unreleased fynd binary.

The matrix uses the existing runner's execution, validation, and reporting code
with larger deterministic fixtures and additional command configurations. The
exact local driver remains at `build/benchmark-request/driver.py`; its source and
hash are also embedded in the portable JSON. It uses an owned temporary fixture
directory and removes that directory on exit. No caches or system settings were
changed, and no tools were installed.

| Shape | Files | Directories | Description |
| --- | ---: | ---: | --- |
| Wide | 100,000 | 1 | One flat directory. |
| Deep | 3,072 | 97 | One chain through depth 96. |
| Mixed | 49,760 | 1,555 | Six-way branching, five populated levels. |
| Many directories | 37,324 | 9,331 | Six-way branching, six levels, four files per directory. |

Files are empty. Every seventh name is hidden; every eleventh contains
`needle`. The selective query matches 9,091, 288, 4,665, and 9,331 files respectively.

## Median elapsed milliseconds, all regular files

| Shape | fynd j1 | fynd j2 | fynd j4 | fynd j8 | GNU find | fd j1 | fd j4 | fd auto |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Wide | 54.47 | 56.61 | 56.25 | 56.34 | 73.55 | 75.14 | 42.73 | 42.44 |
| Deep | 5.21 | 6.00 | 5.85 | 6.61 | 5.11 | 7.03 | 7.18 | 8.06 |
| Mixed | 28.74 | 30.04 | 30.19 | 33.64 | 35.15 | 38.24 | 18.41 | 13.29 |
| Many directories | 69.62 | 69.49 | 71.19 | 87.23 | 109.07 | 87.91 | 56.09 | 25.06 |

## Median elapsed milliseconds, literal `needle` query

| Shape | fynd j1 | fynd j2 | fynd j4 | fynd j8 | GNU find | fd j1 | fd j4 | fd auto |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Wide | 51.86 | 52.06 | 54.10 | 52.27 | 73.08 | 67.89 | 40.00 | 41.34 |
| Deep | 4.52 | 5.32 | 5.85 | 7.47 | 4.75 | 6.84 | 7.24 | 7.92 |
| Mixed | 24.60 | 25.59 | 26.78 | 28.49 | 33.39 | 32.87 | 15.33 | 11.75 |
| Many directories | 68.95 | 69.19 | 69.30 | 81.24 | 104.43 | 81.18 | 53.12 | 22.90 |

## Selected median peak RSS, KiB, all-files query

| Shape | fynd j1 | fynd j4 | GNU find | fd j1 | fd auto |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wide | 13,372 | 13,500 | 32,214 | 41,588 | 12,148 |
| Deep | 2,876 | 2,480 | 3,112 | 6,708 | 8,884 |
| Mixed | 1,724 | 1,852 | 2,728 | 5,492 | 12,596 |
| Many directories | 1,724 | 1,788 | 2,648 | 4,980 | 11,956 |

## Interpretation

Keep fynd serial by default. Its additional listing workers did not show useful
scaling in this warm run, including the 9,331-directory tree. Eight workers were
generally worse. Tiny median differences are not established improvements.

fynd compares well with single-worker fd and GNU find on these selections, but
that is not a default-threading comparison. fd's automatic threading was about
2.8 times faster than fynd j1 on the many-directory all-files query and about
2.2 times faster on mixed all-files. Its peak RSS was higher on those trees.

fynd retains deterministic preorder and serial coordination; file membership
validation does not force the reference tools to reproduce that ordering. The
worker pool parallelizes listing, not all traversal/policy/output stages. Those
are architectural facts, not a profiling result that proves the bottleneck.

Next useful evidence is a profile of the branching workload and realistic or
high-latency storage. This run makes no claim about cold caches, ignore-engine
cost, network filesystems, or universal tool performance.
