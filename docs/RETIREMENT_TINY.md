# Retirement tiny-control follow-up

Decision: INCONCLUSIVE_CALIBRATION. The original
[retirement experiment](READY_RETIREMENT.md) remains NOT_RETAIN. This follow-up
neither clears nor promotes its candidate. No C changes, rebuilds, full
qualification, auto mode, skew work, or publication occur.

## What the fixed follow-up finds

The identical-baseline alias fails the predefined calibration rule in 13 of
48 case/thread/wave comparisons. Its point-median ratios range from 0.8515 to
1.1308 despite using the exact same baseline path, SHA, argv, environment,
working directory, and output treatment. Paired alias medians also fail the
bounds in some cells. The rule requires BOTH point and paired alias medians
within [0.95,1.05] in every cell, so calibration takes precedence over any
candidate interpretation.

The original two flagged cells do not exceed 1.05 in either point or paired
candidate medians in either new wave:

- Empty no-result file, j8: point ratios 0.9644 and 0.9859, paired 0.9828 and 1.0015.
- Tiny all-results file, j32: point ratios 1.0272 and 0.9840, paired 1.0350 and 1.0129.

But new repeated candidate point flags appear for empty no-result file j32 and
tiny no-result pipe j32. They remain visible below. With failed alias calibration,
these do not establish causal code regressions, and absence of the original
flags does not establish that the candidate is regression-free. No cause is
assigned to the alias variation, and no host-noise explanation is claimed.

The measurement stops here. No extra seed, larger repetition count, new binary,
post-hoc threshold, or repeat-until-favorable follow-up is introduced. A different
measurement method or a different startup-cost budget would require a new
prospective decision, not reinterpretation of this run.

## Fixed protocol and counts

Two waves use seeds 3109 and 6203. Each has 108 repetitions of eight queries:
empty/tiny5, all/none, file/pipe. Nine labels cover baseline, candidate, and an
identical-baseline check at j1/j8/j32. Every query uses --no-ignore, -H, -0, and
-t f. The alias is a label, not a copied or rebuilt executable.

Twelve randomized Latin blocks use all nine cyclic rotations. Each label
occupies each position exactly 12 times per case in each wave. Case-group order
also varies with the fixed seed. One baseline-j1 warmup precedes each group,
not selected labels. All 72 prechecks pass before timing starts in each wave.

Each wave retains 7,776 native samples and 864 validated warmups. Combined:
144 prechecks, 15,552 timings, 1,728 warmups, 144 separate distributions, and
48 comparison cells. All 17,424 output receipts and 34,848 journal rows pass
checks. Waves and configurations are never pooled.

## Ratios and counterevidence

Ratios are relative to baseline at the same thread setting. Point ratios divide
elapsed medians; paired ratios are medians of the 108 same-repeat ratios.
The table includes the four original cells, all 13 calibration failures, and
both waves of the two new repeated candidate flags, with duplicates shown once.
All 48 comparison cells and complete distributions remain in the archive.

| wave | case | threads | candidate_point | candidate_paired | alias_point | alias_paired |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | empty-all-pipe | 1 | 0.9642 | 0.9674 | 0.8515 | 0.9621 |
| 0 | empty-all-pipe | 32 | 0.9681 | 1.0014 | 0.9018 | 0.9679 |
| 0 | empty-none-file | 8 | 0.9644 | 0.9828 | 1.0017 | 0.9951 |
| 0 | empty-none-file | 32 | 1.0599 | 1.0024 | 1.0777 | 1.0682 |
| 0 | empty-none-pipe | 32 | 1.0821 | 1.0744 | 1.0574 | 1.0059 |
| 0 | tiny-all-file | 32 | 1.0272 | 1.0350 | 1.0088 | 0.9944 |
| 0 | tiny-all-pipe | 1 | 0.9297 | 0.9922 | 1.1308 | 1.0146 |
| 0 | tiny-none-pipe | 1 | 0.9470 | 1.0027 | 0.8891 | 0.9429 |
| 0 | tiny-none-pipe | 8 | 0.9583 | 0.9651 | 0.9425 | 0.9836 |
| 0 | tiny-none-pipe | 32 | 1.0537 | 0.9788 | 1.0274 | 0.9723 |
| 1 | empty-all-file | 8 | 0.9739 | 0.9874 | 1.0375 | 1.0513 |
| 1 | empty-all-pipe | 32 | 1.0554 | 1.0401 | 0.9477 | 0.9896 |
| 1 | empty-none-file | 1 | 1.0057 | 1.0451 | 1.0623 | 1.0733 |
| 1 | empty-none-file | 8 | 0.9859 | 1.0015 | 0.9638 | 0.9823 |
| 1 | empty-none-file | 32 | 1.0802 | 1.1033 | 1.0408 | 1.0059 |
| 1 | empty-none-pipe | 1 | 1.0349 | 1.0311 | 1.0594 | 1.0460 |
| 1 | tiny-all-file | 1 | 1.0001 | 0.9800 | 0.9402 | 0.9394 |
| 1 | tiny-all-file | 32 | 0.9840 | 1.0129 | 0.9667 | 1.0071 |
| 1 | tiny-all-pipe | 8 | 0.9682 | 0.9622 | 0.9461 | 1.0074 |
| 1 | tiny-none-pipe | 32 | 1.0822 | 1.0528 | 1.0264 | 1.0434 |

## Unchanged inputs and checked behavior

The baseline is the exact qualified `run.vbgYhn` binary. Candidate path, binary
SHA, and all eight application/build input hashes match BOTH original successful
retirement waves. There is no candidate patch or rebuild between or during these
experiments. All 102 qualified working inputs, archived c-utils contents, tool
hashes, and source records match before and after both new waves.

All prechecks, warmups, and timings use real file or pipe captures, never
`/dev/null`. Independent creation metadata defines the five tiny records and
empty results. Checks cover NUL framing, multiplicity, root group, expected
canonical digest, count, and raw-output digest. The analyzer checks the finite
set of possible five-record permutations. Successful raw scratch captures are
reused; their hash receipts are not retrospective rehashes of every raw stdout.

CPU counters preserve user+system sums and mean-core arithmetic and are checked
against unchanged GNU-time counters. Synthetic controls reject coherent CPU
forgeries, truncated output, duplicate records, source drift, bad calibration,
and bad candidate gate conditions. Boundary-equality gate sensors also pass.
This is benchmark evidence, not a new release qualification or mutation score.

Before any finder execution, source review catches a missing `re` import in
an updated environment validator. The import is added and the actual validator
is exercised with valid metadata, malformed SHA, and an extra retained environment
key. All preflight controls pass before the first timing. No completed wave is
rerun because of that prelaunch fix.

The driver compares the full execution environment in memory for every invocation.
Reports retain only HOME, XDG_CONFIG_HOME, GIT_CONFIG_GLOBAL, GIT_CONFIG_NOSYSTEM,
and LC_ALL, plus a digest of the full environment. Unrelated environment values
and credentials are not written to the reports or data archive. That digest
binds an execution-context receipt, not an independently observable process state.

## Resource limits and interpretation

Every heavy step is sequential in a 3 GiB, zero-swap, nice-15 scope. Core limits
are zero. The immediate shell relay stays alive in the same capped scope; both
relay and driver limits are checked before and after. Disk admission requires
5 GiB. Scope memory-max, OOM, and swap counters stay zero in both native waves.
Only newly owned fixtures are removed, and cleanup is verified.

No security, affinity, cache policy, dependency, or host limits change. These
are shared-host, heated, process-launch measurements, not controlled-cold,
isolated-host, hardware CPU attribution, or general fd comparison evidence.
The alias failure means this experiment does not meet its own 5% calibration
rule. It does not identify the cause, invalidate the earlier large-package
results, prove a startup regression, or prove zero startup penalty.

The prior release remains selected. c-utils remains pinned to
`eafa795805fa997e2db91f9182c4e0f86fa32fb4`, and HEAD remains
`0c7f8a3a72525d51e04dbb124e6eee4c593cfce3`. The private candidate is still
unqualified and unretained. The earlier NOT_RETAIN decision and its evidence
are left unchanged.

## Retained evidence

- [Index](../bench/results/retirement-tiny.json)
- [Compressed evidence](../bench/results/retirement-tiny-data.json.gz)
- [Unpooled 144-distribution summary](../bench/results/retirement-tiny-summary.csv)
- Plans, scripts, wave logs, original receipts, and decision: `build/retirement-tiny/`

Packed checks use current hash-bound trusted scripts and recorded data, not
execution of archived code or executable binaries. Live checks additionally
rehash original inputs. Hashes and journals are receipts, not cryptographic
binary/source or execution attestations.

```sh
cd /home/travers/projects/fynd
ulimit -c 0
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/retirement-tiny/final_check.py --live'
```

Verified evidence; inconclusive calibration. No promotion, full qualification,
commit, push, install, or publication.
