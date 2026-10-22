# Rejected bounded skew prototype

The counted-parent nested-worker prototype is not retained. It improves a
single-level dominant branch, but misses the predeclared smaller-tree regression
gate and does not improve the extra-level skew control. Production remains the
qualified shared-credit implementation from `run.FyCw0V`.

## Native comparison

Two independent runs used the exact current qualified baseline and one isolated
prototype binary. Each has 16 workload/sink cases, nine configurations, and nine
rotated repetitions. Configurations are baseline/prototype j1/j4/j8/j16 and fd
automatic threading. Totals are 288 prechecks, 2,592 timed outputs, and 288 separate
validated warmups. Every fynd output matched independent creation-manifest DFS
bytes. fd passed selected-file multiset validation, not ordered-output parity.

Median elapsed milliseconds at j8, file sink unless marked pipe:

| Workload | Run 1 baseline | Run 1 prototype | Run 2 baseline | Run 2 prototype |
| --- | ---: | ---: | ---: | ---: |
| Dominant branch, all | 95.08 | 78.27 | 96.26 | 74.27 |
| Dominant branch, no matches | 89.92 | 67.20 | 88.83 | 66.05 |
| Extra-level skew, all | 105.77 | 100.39 | 93.63 | 93.75 |
| Extra-level skew, no matches | 88.79 | 87.98 | 89.64 | 88.92 |
| Balanced, all | 51.87 | 51.75 | 50.52 | 46.04 |
| Balanced, all, pipe | 50.63 | 44.57 | 48.75 | 48.22 |
| Smaller branching tree, all | 19.07 | 20.13 | 18.42 | 20.12 |
| Smaller branching tree, no matches | 18.05 | 18.88 | 17.28 | 18.57 |

The dominant-branch quiet gain is about 25% in each run, exceeding the required
15%. Smaller-tree all-results medians regress 5.6% and 9.2%, exceeding the declared
5% repeated-regression limit. CPU medians also rise from 91.2 to 103.8 ms and
91.0 to 102.0 ms there, about 14% and 12%. These costs fail retention even though
some balanced and output-rich cases improve.

There is timing uncertainty. Exploratory paired small-all j8 ratios have medians
1.085 and 1.006; their nominal bootstrap intervals are 1.046 to 1.097 and 0.996 to
1.164. Ratios of independent wall medians and medians of paired ratios differ.
The rejection uses the declared repeated wall-median gate, supported by repeated
CPU overhead, not a claim that every sample or both intervals exceeds 5%.

The skew gain also costs CPU. Quiet dominant-branch j8 CPU medians rise from
90.2 to 121.8 ms and 89.4 to 119.6 ms. More concurrency does not mean less work.
Prototype quiet-skew medians at j4/j8/j16 are 65.2/67.2/82.3 ms in run 1 and
63.4/66.0/82.1 ms in run 2. More threads did not improve this design.

## Designs and implementation

Two designs were compared before coding:

- Nested fork/join keeps one recursive walker, with counted parent tasks waiting
  for ordered child streams. It needs task-tree admission, child-to-parent batch
  relay, logical cancellation, and borrower acknowledgements.
- Resumable frontier fragments avoid waiting parent workers, but require persistent
  frames, reference ownership, and resumable traversal and diagnostic emission.
  It is a substantial rewrite with its own bounded-lookahead limits.

Same-family reviews favored the smaller nested experiment. They rejected its
initial counted-main-holder policy because it would break the existing j2 overlap
barrier and underutilize balanced trees. The implemented revision keeps the
existing coordinator, preserves j2/j3 admission, and limits root jobs to N-2 for
N at least four. Only the current draining head may atomically offer its first
two descending children. Every worker parent remains a counted task; there is no
inline helper or hidden DONE refund.

The prototype reuses N MiB payload in reserved/shared 256 KiB credits. Relays pin
the child credit while copying into existing parent credits, including space
waits. Main remains the only actual FILE writer. Local terminal failures cancel
descendants and await borrower acknowledgements before parent arena scopes end;
only main joins threads. The seed arena releases before a slot becomes FREE,
because offers now come from workers as well as main.

### Why the deeper skew remains serial

This is a structural capacity trace, not hardware-counter attribution. At j8,
six root jobs occupy six tokens: the heavy head and five later roots. The head
can offer two children, bringing pending to eight. If its first child contains
the real heavy tree and the second child is tiny, that heavy child cannot obtain
two FREE slots. The parent is waiting, and later DONE roots/child still retain
tokens until ordered consumption. The real heavy tree falls back to serial.

The first fixture puts the balanced heavy tree directly under root `a`; the
second inserts another heavy-first directory level and tiny sibling directories.
They contain 111,986 and 112,000 files. The balanced and smaller controls contain
298,592 and 37,324 files. Flat 10k, deep-96, empty, no-match, serial, and pipe
controls are also retained. File contents are empty; this is not a live-repository
or full ignore-policy benchmark.

## Verification and limits

The isolated prototype passed strict C89 native builds, six C executables,
508 CLI checks, 140 ignore checks, and the targeted Dependable C fast check.
The new deterministic test proves two nested children overlap despite five future
DONE roots. It enumerates every token and credit owner, holds simultaneous main
IO and relay loans, checks pins through a parent space wait, forces actual short
FILE writes and local terminal failures, and checks borrower acknowledgements
before parent scope release. It also sweeps transactional pair-seed failures.

An initial test compile error called the renamed fixture entry under its old name.
Correcting that test call required no implementation change or diagnostic suppression.
Failed and successful logs remain. Existing overlap/stream/credit/failure tests
were not weakened. Read-only review found no blocking implementation defect, but
queued local cancellation and deeper relay chains remain explicit test gaps.

No full sanitizer, platform, coverage, semantics, fuzz, or mutation qualification
is claimed for this rejected prototype. Those gates were not bypassed for a
retained change; retention stopped at the failed native gate. Current production
still matches all 105 inputs captured before the experiment, including the 100
inputs of its earlier qualification. No dependency, thread default, output order,
job bound, staging capacity, commit, push, install, or publication changed.

## Evidence and replay

[Portable index](../bench/results/skew-traversal.json),
[compressed data](../bench/results/skew-traversal-data.json.gz), and
[summary CSV](../bench/results/skew-traversal-summary.csv) retain both runs,
individual measurements, argv/order, tool/source identities, prototype source,
designs, tests, and replay/audit scripts. Native runs are
`build/skew-traversal/run-3s2_y4yy` and `run-pn_4i0yt`.

The audit checks JSONL equality, output counts/lengths/exact fynd hashes, CPU
arithmetic against independent GNU time, distributions, order, source/tool hashes,
fixture cleanup, and unchanged production inputs. A coherently doubled CPU record
fails the independent timer check. Reused scratch outputs mean fd's raw output
cannot be revalidated afterward from scalar hashes; each invocation passed its
multiset validator before scratch reuse.

Both runs used actual 3 GiB/no-swap/nice-15/core-0 limits and sequential
configurations. The driver refuses heavy execution below 5 GiB free space. No
historical or unrelated files were deleted. Only newly owned fixture trees were
cleaned. Host load was uncontrolled, roughly 3.25 to 4.46 across the runs. No
cold-cache, total RSS-cap, native perf-attribution, or universal fd-parity claim.

Local replay needs the retained binaries and pinned archive paths:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/skew-traversal/experiment.py --offset 0'
```

Do not replace production with this prototype. The subsequently authorized
[continuation experiment](FRONTIER_TRAVERSAL_EXPERIMENT.md) improves the deeper
case, but also fails its control-regression gate. Neither prototype is retained.
