# Research agenda

The first implementation resolves matching, CLI defaults, ordering, ownership,
and error policy in [DESIGN.md](DESIGN.md). This checklist updates the original
research-only README. Checked items describe implemented decisions, not a claim
that every platform or workload has been verified.

The [full streaming benchmark](STREAMING_FULL_BENCHMARK.md) completes the existing
107-workload matrix without application changes. Fixed j8 beats fd-auto by point
median on 81/86 primary and 18/21 matched workloads. Python package-layout and
dominant-branch cases remain slower. Captured layouts are historical metadata
replays, not fresh live-content searches. No new optimization starts from this result.

## CLI and compatibility

- [ ] Compare everyday searches with fd and GNU find. Record deliberate
      compatibility differences rather than promising either command language.
- [x] Choose literal basename substring matching for the first slice.
- [x] Define default and multiple roots, ASCII case folding, extension unions,
      option boundaries, and patterns beginning with `-`.
- [x] Define statuses, including success with no results.
- [ ] Check tool, package, and executable name collisions before distribution.

## Traversal and ignore rules

- [x] Specify static symlinks, broken links, explicit roots, and race limitations.
      Do not promise a filesystem snapshot or sandbox. Mounts are not excluded.
- [x] Define hidden defaults and explicit-root exceptions, independently of
      ignore negation.
- [x] Implement ancestor/repository ignore precedence, unreadable-source policy,
      and `--no-ignore`, reusing cgrep's private byte matcher. Keep application
      policy out of the concurrently developed c-utils library.
- [ ] Extend Git config, worktree gitdir, pattern fidelity, and capability-limited
      physical ancestry only with explicit contracts and reference fixtures.
- [x] Replace mandatory sorted preorder with [qualified streaming](STREAMING.md).
      Output within each root is unspecified; root order and duplicates remain.
      Late enumeration errors keep already emitted results and report failure.

## Filesystem and memory contracts

- [x] Audit CuDir entry lifetimes, copied names, kinds, and errors. Root metadata
      currently needs POSIX lstat; directory-close errors are not observable.
- [x] Add native long-path selection/order fixtures and over-PATH_MAX root
      errors with complete diagnostics and later-root continuation. Checked
      arithmetic also has exact last-fit/first-overflow tests.
- [ ] Extend long-path filesystem qualification across targets. OS limits
      remain filesystem errors, not a promised maximum path length.
- [x] Choose scoped recursion with an explicit depth policy and subtree cleanup.
- [x] Define filesystem, allocation, output, partial-result, and diagnostic policy.
- [x] Select Linux as the initial native target. Keep C89 source portability
      separate from filesystem and runtime-platform coverage.

## Output and pipeline safety

- [x] Define raw newline and NUL output, including unusual filename bytes.
- [ ] Research optional terminal-safe rendering separately. Raw output is not
      safe terminal rendering and must not silently change pipeline paths.
- [x] Keep normal SIGPIPE behavior. Stop after observed output failure, check
      final flush, and preserve caller FILE error indicators.

## Correctness and performance

- [x] Implement an independent Python filesystem oracle for ordered byte output
      and status, without importing the production walker or matcher.
- [x] Add deep, wide, empty, hidden, symlink, FIFO, unusual-byte, and fault
      fixtures for the selected contract. Ignore precedence is out of scope.
- [x] Add bounded pure matcher/path/parser fuzz targets and dedicated semantics
      cases. These do not establish whole-program filesystem correctness.
- [x] Provide strict C89 native tests, deterministic allocation failures,
      sanitizer profiles, coverage, and boundary checks.
- [x] Establish a validated serial warm-cache baseline against fd and GNU find,
      with versions/hashes, fixtures, ordering, cache policy, wall time, and RSS.
      See [BENCHMARKS.md](BENCHMARKS.md).
- [x] Broaden synthetic scales through one million files, captured tracked and
      package layouts, thread counts, output sinks, and matched filesystem/ignore
      controls. See [the comprehensive benchmark](COMPREHENSIVE_BENCHMARK.md).
- [ ] Measure live repository/ignore behavior, controlled cold-cache conditions,
      and slower storage. Captured path layouts do not establish those results.
- [x] Keep serial default. The first opt-in CuThread listing pool preserved
      order and global pending bounds but showed little useful scaling.
- [x] Replace listing-only prefetch with bounded, ordered sibling-subtree jobs.
      Qualify final-tail delivery, output backpressure, DONE-inclusive bounds,
      startup/allocation failures, and join-before-parent-scope cancellation.
      The live ancestor-memory allowance is explicitly larger.
- [x] Measure the qualified coarse walker on balanced, prefixed, skewed, quiet,
      and output-heavy warm fixtures. Balanced branches improve substantially;
      skew remains a limitation. See [results](COARSE_TRAVERSAL.md).
      Do not enable threading automatically or infer real-repository/cold-cache gains.

- [x] Replace dedicated task mailboxes with reserved/shared batch credits under
      the same N-job and N-MiB payload bounds. Measure quiet, skew, small, and
      pipe controls; disclose the smaller-tree regression and memory tradeoff.
      See [the performance pass](PERFORMANCE_PASS.md).
- [x] Reject impossible first-literal ignore matches without changing parser
      state, glob syntax, precedence, allocations, or fixed work limits. Keep
      wildcard-leading/colliding-byte controls and charged-work sensitivity.

- [ ] Improve dominant-branch redistribution without unacceptable control costs.
      The first [counted-parent prototype](SKEW_TRAVERSAL_EXPERIMENT.md) was rejected:
      about 25% quiet-skew improvement, no extra-level skew gain, and smaller-tree
      j8 regressions. The [continuation prototype](FRONTIER_TRAVERSAL_EXPERIMENT.md)
      improves both skew shapes but also fails control regressions. Its added
      per-directory management was [profiled and reduced](FRONTIER_COST.md) on
      leaf-only directories. CPU improves about 6% versus the prototype but
      remains too costly; the reduction is also rejected. Moving persistent
      [disposal outside the scheduler mutex](FRONTIER_RECLAIM.md) cuts smaller-tree
      switches about 70% and brings all-results timings within 4% of production.
      Quiet-small medians still miss the 5% gate, so this candidate is not promoted.
      A subsequent [one-poll subtraction](FRONTIER_POLL.md) verifies less work but
      still fails smaller-tree native controls; it is also not promoted.
      The final [contention pass](FRONTIER_CONTENTION.md) separates stop-read
      synchronization. Quiet j8 controls improve, but repeated j16 regressions
      fail the same gate. Continuation work remains paused.
- [x] Qualify large-listing storage improvements. The
      [gated record-arena change](LISTING_QUALIFICATION.md) reduces million-file
      peak RSS from about 63 to 39 MiB. The initial noisy tiny flag remains in
      the [historical experiment](LARGE_LISTING.md); focused unchanged-binary
      validation did not reproduce it. Full required qualification passed and
      four verified paths were retained locally without commit or push.
- [x] Reprofile the retained build and measure observed first-byte latency.
      The [prefix-aware fallback experiment](LISTING_LATENCY.md) misses its
      benefit gate and stays isolated. Plain million-file names do not use
      that fallback; collection remains the larger measured phase.
- [x] Attribute collection work on the retained build. The
      [collection-cost pass](COLLECTION_COST.md) stops without a speculative
      application patch. Guarded fast append remains an option, not evidence
      of the required native gain.
- [x] Qualify [unordered streaming against fd-auto](STREAMING.md), including
      the approved partial-result error policy. Serial warm million-file file
      and pipe cases beat fd-auto in both matched waves; tiny-control follow-up,
      unchanged required profiles and local working checks pass.
- [ ] Broaden that comparison beyond the measured warm synthetic workloads.
      No universal fd win, isolated-host or cold-cache result is established.

## Packaging and project setup

- [ ] Choose a license compatible with the intended c-utils integration.
- [x] Pin the tested dependency revision and provide a native build without
      requiring the optional checker toolchain.
- [x] Separate local checks from capped full acceptance. Ignore generated
      artifacts and live agent state; never put credentials in Git.

## References

- [c-utils API and ownership](https://github.com/Trav55555/c-utils)
- [cgrep traversal and ignore implementation](https://github.com/Trav55555/cgrep)
- [fd](https://github.com/sharkdp/fd)
- [GNU Findutils manual](https://www.gnu.org/software/findutils/manual/)
- [Git ignore rules](https://git-scm.com/docs/gitignore)
- [Dependable C](https://dependablec.org/)
