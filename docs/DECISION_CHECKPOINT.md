# Retained streaming decision checkpoint

Keep the qualified unordered-streaming implementation selected by `run.vbgYhn`.
The completed-task retirement candidate remains rejected. Later measurements did
not clear it, identify the earlier startup delay's cause, or justify a code fix.
Startup timing investigations are stopped. The existing 5% relative policy is
unchanged; no absolute startup budget has been adopted.

This checkpoint consolidates the current decision without replacing historical
plans, failed gates, receipts or source snapshots. It prepares the retained
version for [review](REVIEW_CHECKPOINT.md), not release or publication.

## Selected implementation

The active executable retains its
[streaming qualification](../build/streaming/qualification/build/verification/run.vbgYhn/).
The current source and build safeguard passed
[`run.z7sAXq`](../build/pinned-build/candidate/build/verification/run.z7sAXq/), exit 0.
All 105 qualified fynd inputs match the current local files and that archive.
The six application C/header inputs match the earlier streaming qualification;
the build, tests and documentation changed. The immutable c-utils revision is
`eafa795805fa997e2db91f9182c4e0f86fa32fb4`.

The selected executable is
`build/streaming/qualification/build/verification/run.vbgYhn/source/fynd/build/native/fynd`,
SHA256 `fe00da21f4b84ae6d5860d3faca22493fe07a9cce977d5f4f27d18671a977237`.
The active local `build/fynd` is byte-identical to the selected archived executable
after user-authorized activation. The previous binary and checkpoint are preserved
in [the activation backup](../build/live-binary/activation-_ap1x71n/before/).
The active path passed 508 CLI checks and 140 ignore checks without a rebuild.
[Activation receipt](../build/live-binary/activation-_ap1x71n/receipt.json).
Matching binary hashes establish byte identity, not compiler attestation.

The [build-safeguard receipt](../build/pinned-build/receipt.md) records full
qualification before retention: 17 build-boundary tests, six C suites, 6,656 CLI
checks, 34 benchmark-helper tests, 25 compile-valid behavioral controls and every
required deep profile. Four semantics and four bounded fuzz targets ran.
Per-module coverage remains above the unchanged 90% floor: coarse 95.33%,
main 96.90%, ignore 95.93%, listing 91.95%. Optional authority, regions and CBMC
were explicitly unconfigured, not passes. The safeguard changed no application C,
performance policy or live executable. [Build details](PINNED_BUILD.md).

The retained contract allows unspecified order within a root. Root argument order,
duplicate multiplicity and contiguous records remain strict. Ordinary late
traversal errors preserve prior output, diagnose, return status 1 and continue
reachable work. Resource and output failures remain terminal.

Serial is the default, with no thread primitives for explicit j1. Parallel output
still uses bounded FIFO whole-task delivery, not any-ready chunk interleaving.
DONE-but-unread tasks remain charged. In threaded mode, N configured threads
allow at most N outstanding tasks and N MiB of aggregate payload. These limits
exclude other memory and descriptor costs. Workers cannot share their descendant
traversal. [Streaming details](STREAMING.md) retain the full contract,
qualification history and limitations.

## Why later work did not replace it

| Work | Recorded outcome | Consequence |
| --- | --- | --- |
| [Completed-task retirement](READY_RETIREMENT.md) | NOT_RETAIN | Package gains did not override repeated small-control flags. Full candidate qualification was not run. |
| [Unchanged-binary tiny follow-up](RETIREMENT_TINY.md) | INCONCLUSIVE_CALIBRATION | Identical-baseline alias calibration failed in 13 of 48 cells. No clearance or causal noise conclusion. |
| [Direct-CLI virtual-label pilot](../build/cli-aa-pilot/receipt.md) | INSUFFICIENT_PRECISION | The prospective precision gate failed. All 24 observed contrasts within 5% did not override that result. |
| [Existing-data diagnosis](../build/cli-aa-diagnosis/receipt.md) | Descriptive only | A few uneven units contributed much of individual strata's conditional variance. No observations were removed. |
| [Launcher phases](../build/cli-phase-probe/receipt.md) | DIAGNOSTICS_ONLY | Most elapsed in the longest sampled j8 calls occurred after spawn return. Child work and parent scheduling remained unresolved. |
| [Exit-observation trace](../build/cli-exit-probe/receipt.md) | DIAGNOSTICS_ONLY | All 44 lifecycles matched. No large post-exit observation gap appeared in this perturbed trace. Historical cause remained unknown. |

The virtual-label pilot held all actual executable paths and arguments identical.
It measured conditional precision, not candidate effects, real-path bias or future
statistical power. R95 is the 95th percentile of the maximum absolute mean-log
contrast across the 24 strata under balanced virtual-label redraws.
The reference value was 0.047283615598 versus a fixed target of 0.024395082085,
half of log(1.05). The 5,760 primary timings and 24 strata remain intact.

The phase diagnostic retained 644 baseline invocations. The separate trace retained
44, with a largest raw exit-notice-to-wait observation gap of 55.755 microseconds.
Twenty-two wait returns were reconstructed from complete syscall durations;
twenty-two used resumed-call timestamps. Ptrace pauses processes, and its exit
notification is not an unperturbed child-exit instant. Neither diagnostic explains
the earlier delay or supplies native CPU-function attribution. Their timings are
not pooled with the original wrapped benchmark endpoint.

Genuine Claude Opus 5.5 review informed the protocol changes. The
[lead judgment](../build/claude-protocol-review/lead-judgment.md) separates useful
recommendations from unsupported probability and causal claims. Neither that
review nor subsequent same-family reviews independently attest the artifacts.

## Remaining limits and decisions

The [full streaming benchmark](STREAMING_FULL_BENCHMARK.md) covers 107 workloads,
including explicit package and skew losses. It does not establish universal fd
superiority, isolated-host behavior, controlled-cold performance or cache residency.
Payload accounting does not include all cursor, policy, stack or descriptor costs.
Historical successful scratch outputs were reused; retained hashes and journals
cannot retrospectively rehash every original output. The newer pilot and diagnostic
captures were retained and re-read, as their separate receipts specify.

No new candidate run follows from this checkpoint. Further optimization requires
an explicitly chosen objective and prospective protocol. Any different startup
budget is a user policy decision, not a correction to the old failed gate.
The tracing route is closed rather than extended until a rare delay appears.

Everything remains local and uncommitted. HEAD is
`0c7f8a3a72525d51e04dbb124e6eee4c593cfce3`.
Most implementation and evidence paths are untracked; `git diff` alone omits them.
The linked `build/` archives and receipts are ignored local artifacts, unavailable
in a fresh clone. No commit, push, global installation or publication is authorized.
