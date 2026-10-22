# Final-component ignore matching

Retained. Unanchored ignore rules now match only the final path component, and
directories without loaded rules no longer take part in per-entry evaluation.
Verdicts are unchanged; charged matcher work differs and is almost always lower.
Qualified by `build/verification/run.YOdmVS`, exit 0.

## Problem

A profile of a serial run over a tree of Git worktrees (`callgrind`, 532 rules in
nested `.gitignore` files) attributed about 80% of instructions to
`ignore_file_verdict` and `ignore_glob`. Ignore evaluation more than doubled the
walk: 164 ms against 75 ms with `--no-ignore`. Three costs dominated:

- Each unanchored rule was retried at every `/` suffix of the repository-relative
  path, after a charged full-path scan built a first-byte prefilter set.
- Every entry evaluated all three rule classes in every ancestor frame up to `/`,
  although almost every frame loads no rules.
- The per-byte work budget (`ignore_tick`) is charged inside those scans.

## Change

`ignore_parse` already marks any glob containing `/` as anchored. An unanchored
glob is therefore one component. For such a glob `ignore_glob` returns `pc == pl`,
`1` for `**`, or `pl == 0` for an empty glob, so only the final suffix can match.
`ignore_file_verdict` now finds the final component once, lazily, with a charged
backward scan. It matches unanchored rules against that component only. The
first-literal-byte rejection compares against the component's first byte.
`IgnoreRule` documents the invariant.

Empty rule files return before any work. `ignore_verdict` skips
`ignore_relative` for frames whose three rule files are all empty. Empty files
never decide and never charge work, so repository bookkeeping, precedence and
global-file lookup are unchanged.

## Work accounting

Verdicts are equivalent; work totals are not. The final-component scan is
charged once per file verdict for any eligible unanchored rule. The former
full-path scan was charged only for literal-first rules. A matching `*` rule on
`a` now costs 8 ticks instead of 7. Long relative paths cost far less. An
independent 3-million-case probe found no verdict difference. With small random
budgets, 55,128 cases now finish where the former matcher exhausted its budget,
and 1,037 exhaust where it finished. The extra cost is at most one final component
per verdict, against `IGNORE_WORK_LIMIT` of 1,000,000. As before, identical tick
totals or resource-failure observations are not promised.

## Test changes

- Hand-built rules in `tests/test_ignore.c` and `fuzz/fuzz_ignore.c` now honor the
  parser invariant through `parser_anchored` or the equivalent fuzz expression.
  Two finite-table rows previously described unanchored globs containing `/`,
  which the parser cannot produce. They now use the anchored form the parser
  produces, where an inner-suffix match is 0.
- The charged-work sensor uses a 128-byte final component. It still rejects one
  scan per rule and a full-path scan. The `*` case pins its exact 8-tick cost.
- `tests/sensitivity.py` retargets `invert-necessary-start-filter` to the new
  rejection and adds `skip-frames-without-plain-rules`, which drops frames that
  hold only Git-class rules. 26 named controls are expected.

## Evidence

- `run.YOdmVS`: build guards, six C suites, three 500-case CLI seeds (6,324
  checks), 332 ignore CLI checks and required deep checks pass. All 26 named
  behavioral controls reject. Authority, regions and CBMC remain optional skips.
  Coverage: coarse 94.86%, main 96.90%, ignore 95.89%, listing 91.95%;
  combined 95.74%.
- Out-of-tree Git differential: 3,000 random rule sets and trees, with nested
  `.gitignore`, `**`, negation, escapes, dir-only rules and odd names. fynd
  `-t f` output matched `git ls-files --others --exclude-standard` in every case,
  before and after the change. Injecting `--no-ignore` produced 170 mismatches in
  400 cases, so the harness detects missing filtering.
- Output and diagnostics were byte-identical to the prior selected binary,
  serial and `-j8`, on a 404k-result home project tree.

Warm medians, nine runs, same host, prior selected binary (`fe00da21…`) against
the `run.YOdmVS` binary. Pairs ran back to back without rotation. These are
descriptive, not significance claims.

| Case | Before CPU ms | After CPU ms |
| --- | ---: | ---: |
| Worktree tree, 82k results, ignore on, j1 | 174.6 | 125.1 |
| Home projects, 404k results, ignore on, j1 | 1,637.4 | 1,236.5 |
| Home projects, ignore on, j8 | 1,904.5 | 1,380.8 |
| `/usr`, ignore on, j1 | 436.5 | 408.9 |
| `/usr`, `--no-ignore`, j1 | 310.2 | 304.2 |
| Python dist-packages, j1 | 11.9 | 11.1 |

## Activation and open work

The active `build/fynd` is now the `run.YOdmVS` executable (SHA256 prefix
`85a0d2d6ab8211d6`), activated with user approval after it passed 508 CLI checks
and 140 ignore checks without a rebuild. The prior binary is preserved in
`build/live-binary/activation-YOdmVS/before/`. Remaining ignore costs, not pursued here,
are the per-entry absolute-path mapping in `ignore_map` and the per-byte work
budget. Precompiling common rule shapes (`*.ext`, literal names) is also open.
Threaded scaling is a separate, larger problem.
