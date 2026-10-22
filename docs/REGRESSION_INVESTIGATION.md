# Focused regression follow-up

The earlier large regressions did not reproduce in two narrower paired runs.
No application code or tuning changed. This does not erase the earlier results
or prove that their entire slowdown was host noise.

The investigation targets the smaller branching-tree j8 increase from 22.64 to
27.89 ms and balanced-tree serial increase from 178.14 to 210.02 ms in the
[performance pass](PERFORMANCE_PASS.md). It does not start the proposed skew
rewrite, million-file optimization, or live-repository campaign.

## Method

Compare exact qualified binaries from `run.XXAMxe`, `run.YwHq6N`, and `run.FyCw0V`:
old dedicated mailboxes, shared 64 KiB batches, and current shared 256 KiB batches.
Current source still matches all 100 captured inputs of the last qualification.

Each independent wave creates the same smaller tree with 37,324 files and the
same balanced tree with 298,592 files. Both have 9,331 directories. Each tree
runs all-result and no-match queries, file and captured-pipe sinks, and j1/j8.
There are six configurations and 24 repetitions. Each configuration occupies
each position four times per case. The driver reshuffles the base order between
six-repeat blocks and changes case order each repetition. Seeds are 1103 and 2207.

Every configuration passes the independent exact-DFS precheck before timing;
every timed output and separate serial warmup passes the same validator.
Both waves together passed 96 prechecks, 2,304 timed outputs, and 384 warmups.
Configurations run sequentially inside the actual 3 GiB/no-swap/nice-15/core-0
limits. There is no cache dropping, CPU affinity change, privileged operation,
output-discard timing, or claim of an isolated host.

## Observed medians

Elapsed milliseconds. Current means the retained 256 KiB build.

| Target | Wave 1 old | Wave 1 current | Wave 2 old | Wave 2 current |
| --- | ---: | ---: | ---: | ---: |
| Smaller tree, all, j8, file | 18.35 | 18.64 | 18.37 | 18.73 |
| Smaller tree, all, j8, pipe | 17.98 | 18.08 | 18.23 | 18.39 |
| Balanced, all, j1, file | 197.17 | 192.54 | 173.27 | 171.72 |
| Balanced, all, j1, pipe | 147.96 | 148.77 | 149.78 | 147.78 |
| Balanced, all, j8, file | 93.56 | 52.13 | 95.14 | 51.39 |
| Balanced, all, j8, pipe | 93.54 | 48.83 | 97.26 | 51.96 |
| Balanced, none, j8, file | 31.11 | 37.50 | 31.98 | 43.69 |
| Balanced, none, j8, pipe | 30.84 | 42.72 | 40.33 | 46.80 |

The reported 23% smaller-tree regression is absent here. Paired current/old
elapsed ratios have medians 1.003 and 1.017 for file output. Their nominal 95%
bootstrap intervals are 0.996 to 1.052 and 0.981 to 1.043. The original 18% balanced
serial increase is also absent. Paired file ratios are 0.980 and 0.991, with
intervals 0.881 to 1.019 and 0.949 to 1.026.

These are medians of per-repeat ratios, not ratios of the independent medians
in the table. Intervals use 4,000 paired resamples with a fixed seed. They are
exploratory, not corrected for multiple comparisons or guaranteed independent
host conditions. Do not treat their endpoints as universal regression bounds.

### CPU versus elapsed time

Balanced serial file-output CPU medians were 148.89/149.44 ms before/current in
wave 1 and 149.37/150.06 ms in wave 2. Its much larger file-versus-pipe elapsed
difference affects both versions. That does not support an 18% intrinsic CPU
regression. It also does not identify a particular kernel or scheduler cause.

The balanced all-output j8 speedup reproduced, with a CPU cost. File-output CPU
medians changed 156.92 to 166.44 ms and 160.46 to 169.82 ms. Paired CPU ratios
are about 1.07 in both waves, while paired elapsed ratios are about 0.56 and 0.55.

Quiet balanced j8 remains variable. Current file and pipe medians were higher,
but paired elapsed intervals overlap parity in both waves and sinks. Quiet CPU
ratios stay near one. This is unresolved waiting/scheduling variability, not an
established CPU regression or evidence that no slowdown is possible.

### Resource observations

Wave loads were 3.94 to 2.65 and 2.65 to 4.49. Scope memory peaks were 881 and
925 MiB, with zero memory-limit/OOM events. CPU quota was unlimited and no CPU
throttling occurred. Cumulative scope IO-full pressure increased about 1.82 and
1.35 seconds over the roughly 120-second timed periods. These aggregate counters
cannot attribute a particular sample's delay. Group-boundary snapshots are not
continuous monitoring.

The narrow fixture working set is smaller than the original 15-tree matrix.
Cache pressure, output writeback, background activity, or matrix-order effects
may matter. No causal claim singles out one of them, and the old measurements
remain intact. Independent GNU time CPU accounting and a coherently doubled-CPU
negative control check the resource-record arithmetic.

## Decision and evidence

Do not change production based on this follow-up. There is no reproduced large
small-tree or serial regression to fix. Keep the 256 KiB default, serial default,
N pending-job bound, N MiB payload bound, and the existing qualification.
If pursuing the quiet-j8 variance, first obtain a tighter two-binary reproducer;
do not jump to a new scheduler or another broad benchmark.

[Portable index](../bench/results/regression-investigation.json) and
[full data](../bench/results/regression-investigation-data.json.gz) retain both
waves, all samples, exact argv/order, source/tool hashes, helper/driver versions,
resource snapshots, paired intervals, and audit results. Raw runs are
`build/regression-investigation/run-_dot0fy5` and `run-ug12xnk6`.

Local replay, with retained receipt paths and the pinned dependency available:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/regression-investigation/paired.py --seed 1103'
```

This is diagnosis, not a new whole-program qualification or a fresh-checkout
installer. No application source, dependency, project history, publication, or security setting
changed. All application guarantees and existing platform limitations remain.
