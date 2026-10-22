# One redundant cancellation poll

Removing one post-classification stop poll saves the expected mutex round trip,
but does not make the continuation candidate meet its performance gate.
Both skew gains survive. Smaller-tree regressions remain, so production is
unchanged and the subtraction stays in an isolated prototype.

## Hypothesis and subtraction

The preceding [reclamation experiment](FRONTIER_RECLAIM.md) moved persistent
payload disposal outside the scheduler mutex. Quiet smaller-tree medians were
still 6-7% slower than production. Three explanations remained plausible:
redundant stop polling, persistent control/ref costs, and shared-host variation.

The private-leaf builder polled after classifying entries. It immediately polled
again at leaf iteration zero, or checked stop under the promotion lock for a
nonleaf. The experiment replaces only that post-classification `walk_stopped`
call with a local `r->stop` guard. No lockless flag, atomic, new mutex, poll
batching, scheduler, buffer, capacity, or admission change was introduced.
Directory-entry and every-64-entry polls remain. Promotion, publication, worker
completion, and coordinator delivery retain their cancellation checks.

A canceled empty directory can now release its private listing without setting
local `Run.stop`. Global cancellation remains set and delivery is blocked. With
a preexisting parent cursor/FILL, some additional bounded private work can occur
before the next existing poll. This is not a promise of identical internal
terminal observations or immediate interruption of synchronous filesystem work.

## Sensitive work and cancellation tests

A new test drives actual `frontier_work` synchronously while real N=2 workers
remain quiescent. Its leaf has explicit raw/hidden NUL-delimited expected bytes.
Private hooks count stop polls under the scheduler mutex and inject global stop
after classification. Separate nonempty leaf and nonleaf cases verify no output
or persistent promotion, with empty private controls and released payload arenas.

Before subtraction, the strict-compiled test passes its output, status,
cancellation, and cleanup checks, prints four polls against a budget of three,
then aborts with status 134. After subtraction the same source passes at three.
The old source and sensor are archived. Compile failure and timeout are not
behavioral rejection. These counts establish less work on that path, not a
native CPU fraction or a throughput improvement.

Nine strict C executables, 508 CLI checks, 140 ignore checks, and targeted
Dependable C fast checks pass. The previous frontier, private-leaf, reclamation,
shared-credit, cancellation, and output tests were not weakened. Same-family
source review found no material correctness bug.

The new model does not exercise concurrent native worker scheduling. Dedicated
empty-child cancellation with a live parent/FILL, hidden-only classification,
and ignore-enabled ordinary/resource-failure cases remain gaps for this change.
The preceding continuation ownership and listing-bound qualification gaps also
remain. No full release qualification is claimed.

## Matched native results

Two separate runs each contain 16 cases, ten configurations, and ten rotated
repetitions: production and candidate j1/4/8/16, preceding reclamation j8, and fd.
Together they pass 320 prechecks, 3,200 timed-output validations, and 320 separate
warmups. Every native output was validated against independent manifests, with
exact DFS bytes for fynd and a multiset comparison for fd.

Median j8 milliseconds, file sink:

| Workload | Run 1 production | Run 1 preceding | Run 1 subtraction | Run 2 production | Run 2 preceding | Run 2 subtraction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Smaller tree, all | 20.94 | 22.80 | 24.85 | 23.93 | 24.46 | 26.76 |
| Smaller tree, quiet | 18.74 | 21.04 | 20.39 | 18.56 | 23.00 | 21.22 |
| Balanced, all | 54.62 | 43.31 | 46.09 | 54.54 | 44.81 | 41.79 |
| Balanced, quiet | 32.35 | 34.98 | 35.35 | 33.50 | 39.34 | 35.17 |
| Dominant branch, quiet | 94.66 | 43.75 | 43.77 | 94.52 | 44.47 | 44.46 |
| Extra-level skew, quiet | 94.66 | 50.76 | 52.08 | 99.44 | 51.32 | 51.43 |

Both quiet skew shapes exceed the predeclared 15% gain over production in both
runs. Smaller all-results regressions are 18.65% and 11.80%; quiet regressions
are 8.80% and 14.34%. These repeated median differences exceed the unchanged
5% control gate. Balanced quiet is 9.28% slower in one run and 4.99% in the other.
Serial dominant-branch all-results file medians also exceed 5% in both runs,
despite no serial-source policy change. The gate is not relaxed for any row.

The preceding reclamation candidate was measured in the same groups. Quiet
smaller-tree time improves about 3% and 8% against it, but CPU does not improve
consistently: preceding/subtraction medians are 104.13/104.17 ms and
103.35/103.91 ms. Smaller all-results CPU is 105.08/107.20 ms and
107.01/106.81 ms. Its elapsed time is worse against the preceding candidate in
both runs. Removing one poll is not enough to justify retaining this variant.

These matrices do not explain the whole regression. Host load rose about
5.16 to 6.74 and 6.74 to 8.15. File-output and unchanged serial timings also vary.
Neither host load alone nor an intrinsic CPU penalty from this subtraction is
established. Nominal paired bootstrap intervals are exploratory, not isolated-host
confidence guarantees. Prior experiments' measurements remain unchanged.

## Decision, audit, and replay

REJECTED_NO_PROMOTION. No full sanitizer/platform/coverage/semantics/fuzz/mutation
qualification follows a failed native gate. Required checks and the 90%
per-module floor remain unchanged for retained code. All 117 frozen production
inputs still match, including the 100 inputs selected by `run.FyCw0V`.
No commit, push, install, dependency edit, publication, cache drop, affinity, or
security-policy change occurred.

[Portable index](../bench/results/frontier-poll.json),
[compressed full evidence](../bench/results/frontier-poll-data.json.gz), and
[summary CSV](../bench/results/frontier-poll-summary.csv) retain source/test
versions, old red and new green logs, per-sample records, measured drivers and
helpers, argv/order, source/tool identities, summaries, and audit/replay scripts.
Local raw runs are `build/frontier-poll/run-huunvhlf` and `run-16e5fo17`.

The audit checks raw JSONL equality, prechecks, distributions, balanced order,
counts/bytes/exact fynd hashes, CPU arithmetic, independent GNU time, final tool
and source identities, fixture cleanup, archive and CSV agreement. Coherently
doubling CPU/user/system/cores fails the independent timer. fd bytes passed
multiset validation before scratch reuse; retained scalar hashes cannot recreate
those bytes afterward. Reviewers use the same model family and do not rehash
raw evidence independently.

Heavy work ran sequentially with actual 3 GiB/no-swap/nice-15/core-0 limits,
a 5 GiB disk preflight, and cleanup limited to newly owned fixtures. No controlled
cold/full-residency, slow-storage, total RSS-cap, native hardware-attribution,
whole-program scheduling proof, or universal fd-parity claim is made.

Local replay requires retained binaries and pinned paths; it does not promote:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c 'ulimit -c 0; exec python3 -I build/frontier-poll/experiment.py --offset 0'
```

A subsequent [contention pass](FRONTIER_CONTENTION.md) keeps all polls and
separates stop-read synchronization. Quiet j8 controls improve, but repeated
j16 regressions still reject the continuation candidate. This ends that
optimization line for now; production remains unchanged.

Partially verified. The work subtraction and measured rejection are verified;
the continuation candidate remains unqualified for release.
