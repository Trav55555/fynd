# Branch-first scheduling experiment

Reject this candidate. Deferring the early ancestor refill eliminated synchronous
child listings, but made the branching workloads slower. Production source and
the working executable remain unchanged from the accepted walk optimization.
Serial remains the default.

[Samples, source variants, probes, binary hashes, and replay scripts](../bench/results/scheduling-experiment.json)
are retained. Raw artifacts are in `build/scheduling/run-c_ixu49k`.

## Candidate and decision

The candidate deletes one `refill(r)` call in `visit()`, immediately before ignore
rule loading and eligibility checks. The existing refill after activating the
current directory remains. This gives that directory's children first access
to a refunded token instead of immediately refilling ancestor siblings.

The job limit, completed-job accounting, output ordering, ignore policy, and
worker authority remain unchanged. The scratch private overlap fixture requires
the nested listing to run on a worker instead of synchronously. Its overlap and
budget assertions remain; the production tests were not changed.

Nine repetitions compared uninstrumented before/candidate binaries at one, two,
four, and eight workers. Automatic fd was a reference. All 72 prechecks and
648 timed outputs matched independent creation manifests. The 64 fynd prechecks
and 576 timed fynd outputs also matched the baseline's exact ordered bytes.

Median all-files elapsed time, milliseconds:

| Shape | Before j1 | Before j4 | Candidate j4 | Before j8 | Candidate j8 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wide, 100,000 files | 55.12 | 54.34 | 54.14 | 53.71 | 53.82 |
| Deep, 96 levels | 4.98 | 6.12 | 6.49 | 6.68 | 7.22 |
| Mixed, 49,760 files | 27.46 | 29.07 | 38.27 | 26.90 | 32.45 |
| Many directories, 9,331 | 66.83 | 68.82 | 149.50 | 80.01 | 147.94 |

Every paired many-directory j4 sample was slower, with ratios from 1.98 to 3.02.
Selective queries regressed there too. Removing the fallback did not improve
throughput, so the candidate stays outside the application working tree.

Conditions were warm ext4 on NVMe, Ryzen 5 7600 with 12 logical CPUs available,
ignores bypassed, hidden names included, regular files, and NUL output. Startup,
GNU time, and output-file I/O count toward elapsed time. Invocations ran
sequentially under the same 3 GiB/no-swap/nice-15/no-core launch settings.
Background activity was uncontrolled; one-minute load moved from 3.50 to 6.82.
This is not a cold-storage or real-repository result.

## What the probes established

Separate instrumented binaries counted listing assignments and sampled pool
state before a synchronous child collection. Three repetitions covered all four
shapes and worker counts. All 96 outputs matched independent manifests and the
native baseline's ordered bytes. Root, synchronous-child, and worker counts
always summed to the independently known directory count.

Median observations for the many-directory shape at four workers:

| Observation | Before | Candidate |
| --- | ---: | ---: |
| Synchronous child listings | 8,910 | 0 |
| Worker listings | 420 | 9,330 |
| Synchronous fallbacks with every token occupied by DONE jobs | 8,587 | 0 |
| Collector-interval fraction of configured worker capacity | 1.69% | 12.99% |

The queue-stranding concern was real. However, sending every small listing to a
worker increased activity without improving native throughput. Synchronization,
prefetch allocation, and handoff costs are plausible contributors; this
experiment did not isolate their native CPU costs. The next experiment should
reduce per-entry work or use coarser worker tasks, rather than maximize the
number of dispatched listings.

Collector-interval fraction is summed wall time around worker collection,
including sorting, I/O, and descheduling, divided by worker count times app wall
time. It is not CPU utilization. It excludes coordinator collection and the
worker's completion-lock reacquisition. Worker CPU measurements separately cover
the worker function, including queue overhead. Peak in-flight counts can include
workers awaiting completion publication; they are not an overlap proof.
Instrumentation can affect scheduling and the sampled DONE counts. Its timings
were never used for native performance comparisons.

## Verification limits

Both native snapshots passed strict C89 builds and all three C executables. The
candidate also passed 508 CLI checks and 124 ignore checks before benchmarking.
The production tree still matches all 49 inputs captured by acceptance
`run.B1HO3G`. That existing acceptance remains current for production code.
No new full checker, sanitizer, TSan, coverage, or fuzz qualification was run for
the discarded candidate.

The initial probe build hit a shadow warning in experiment-only instrumentation.
A renamed global fixed it without suppressions. Failed probe snapshots and logs
remain alongside the successful `probe-v2-*` snapshots. A read-only review found
no material counter-race or decision issue, but identified inconsistent probe
names in the setup script. The replay scripts now agree on names and allow a
separate output/input directory. Fresh preparation reproduced all four measured
source variants by hash; the recorded scripts are preserved separately.

A separate fresh launch checked actual cgroup memory/swap limits, nice level,
and core limit. It verifies the launch settings, not historical kernel state
from the original measurement processes. Nothing changed system security
settings, installed tools, altered dependencies, or published artifacts.

## Reproduce locally

The JSON embeds the recorded and corrected replay scripts, original fixture
driver, and changed source modules. Restore the replay scripts under
`build/scheduling/` and the fixture driver under `build/benchmark-request/` if
those local artifacts are absent. The setup deliberately refuses to overwrite
existing snapshots and verifies the accepted baseline source hashes.

```sh
python3 -I build/scheduling/prepare.py --output build/scheduling/replay
systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
  nice -n 15 bash -c '
    ulimit -c 0
    dep="$PWD/build/verification/run.B1HO3G/source/c-utils"
    for variant in before branch; do
      timeout 180 make -j2 -C "build/scheduling/replay/$variant" CUTILS="$dep" all test || exit $?
    done
    for variant in probe-v2-before probe-v2-branch; do
      timeout 120 make -C "build/scheduling/replay/$variant" CUTILS="$dep" all || exit $?
    done
    exec python3 -I build/scheduling/experiment.py --inputs build/scheduling/replay
  '
```

The local accepted dependency archive must exist, or be recreated from the
recorded immutable revision. The fixture and benchmark-core hashes identify the
helpers. Executable identities are checked before and after measurement, every
result is validated, and owned temporary fixtures are removed on exit.
