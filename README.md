# fynd

A small, read-only file finder in Dependable C, built on
[c-utils](https://github.com/Trav55555/c-utils).

Search basenames with literal text, filter by type or extension, and stream
paths in unspecified order within each root. Unlike [cgrep](https://github.com/Trav55555/cgrep),
fynd finds the files themselves rather than searching their contents.

**Experimental.** Serial by default, with opt-in threaded subtree traversal
and ignore-file support. No regex selection, file mutation, or command execution.
Linux is the initial native target. This is not a complete fd or GNU find replacement.

Native benchmark results are not a full verification pass. Retention requires
the unchanged required checker profiles and at least 90% coverage in each module;
actual acceptance receipts are documented separately.

[Build](#build) · [Usage](#usage) · [Behavior and limits](#behavior-and-limits) ·
[Development](#development) · [Research](docs/RESEARCH.md)

## Build

You need GNU Make, a GCC-compatible C compiler, and Python 3 (standard library
only; override with `PYTHON=...`). Keep the pinned c-utils checkout or extracted
archive beside this repository, or set `CUTILS` to its location.

```sh
make
./build/fynd --help
./build/fynd README .
./build/fynd -t f -e c '' src/
```

For an existing dependency checkout elsewhere:

```sh
make CUTILS=/path/to/c-utils
```

[CUTILS_REVISION](CUTILS_REVISION) and [the content manifest](ci/pinned_cutils.json)
pin exactly 16 consumed inputs: `include/cu.h` and 15 `src/*.c` files. Native
builds reject changed, missing, extra, symlinked, or nonregular entries in those
`include/` and `src/` directories, including symlinked directories. The selected
root itself may be a symlink. This checks consumed content, not Git HEAD or the
rest of the checkout; a Git-free archive works without any Git invocation.

Every native build/test target checks the pin even when its binary is current,
without forcing recompilation. Rebuilds compile a unique private verified copy
and atomically publish only successful regular-file compiler output, preserving
old binaries on failure. The original tree can change after snapshot capture
without changing that build's dependency bytes. Temporary copies are cleaned on
success and ordinary failures; abrupt termination can leave staging debris.
`make clean` needs neither Python nor the dependency.

Native builds need no checker, network, installation, or cache. Compiler/flag
overrides and application inputs are trusted; private paths prevent accidental
races, not hostile same-user writes or compiler behavior. Full verification
continues to extract the immutable pinned Git object into its receipt, without
switching or modifying the sibling checkout.

## Usage

```text
fynd [OPTIONS] [--] [PATTERN [ROOT ...]]
```

The first operand is always a **literal basename pattern**, even if it names
an existing directory. Following operands are search roots. No operands
means an empty pattern and root `.`. No roots also defaults to `.`.

Use an empty pattern to search a root without a name constraint:

```sh
./build/fynd '' src/
./build/fynd -i README .
./build/fynd -t f -e c -e h '' src/ include/
./build/fynd --max-depth 1 '' .
./build/fynd -0 -t f '' src/
./build/fynd -j4 '' .
./build/fynd --no-ignore -H '' .
./build/fynd -- --draft ./notes
```

`fynd .` searches for a literal dot in a basename. It does not mean
"list everything under the current directory". Options precede the first
operand. Use `--` for a pattern beginning with `-`.

| Option | Behavior |
| --- | --- |
| `-i`, `--ignore-case` | Fold ASCII letter case for pattern and extension comparisons. |
| `-t`, `--type KIND` | Select `f` regular files, `d` directories, `l` symlinks, or `o` other objects. Repeat to select a union. |
| `-e`, `--extension EXT` | Select the final nonempty suffix after a dot. Repeat to select a union. Give the suffix without a dot or slash. |
| `-H`, `--hidden` | Include discovered dot-prefixed names and descend into hidden directories. |
| `-0`, `--print0` | End each path with NUL instead of newline. |
| `--max-depth N` | Select through depth 0 to 128. Roots have depth 0. |
| `--no-ignore` | Bypass all ignore sources without enabling hidden traversal. |
| `-j N`, `-jN`, `--threads N` | Use 1 through 32 traversal workers. Default 1 is serial; larger values add a coordinator. |
| `-h`, `--help` | Show help as a standalone invocation. |
| `--version` | Show the version as a standalone invocation. |

Name, type, and extension predicates are ANDed. Repeated types and repeated
extensions each form a union. Filters do not prune unmatched directories.
`.profile` and `a.` have no extension; `.config.json` has `json`. Extension
filters apply to directories and symlinks too, so add `-t f` for files only.
Short-option clusters and `--key=value` forms are not supported. Compact
`-j4` is supported.

### Exit status

| Status | Meaning |
| --- | --- |
| `0` | The requested search completed, including a search with no results. |
| `1` | An observed filesystem, allocation, resource, or output failure made the result incomplete. |
| `2` | Invalid invocation before traversal or result output. A diagnostic or flush failure can instead produce status 1. |

Ordinary filesystem failures, including a late directory enumeration EIO,
return status 1 with a diagnostic, retain already emitted results, and continue
other reachable work and later roots. A failed cursor cannot recover unread
entries; its partial results are not discarded. Resource failures and observed
output failures stop traversal, cancel work, and join workers before borrowed
rules or paths are released. Already emitted output is never rolled back.
On POSIX, SIGPIPE may terminate the CLI instead of status 1.

## Behavior and limits

- Roots are searched in argument order, with each root's records delivered
  before the next root. Within each root, output order is unspecified, including
  in default serial mode. Duplicate and overlapping roots retain multiplicity.
  There is no optional sort flag or legacy sorted walker.
- Each root itself can match, including explicit files, hidden roots, broken
  symlinks, and FIFO entries. Explicit hidden roots do not enable hidden children.
- Static symlinks are listed but never deliberately descended. Trailing root
  slashes are removed except for `/`. Other lexical spelling is preserved.
- Paths retain their root prefix. Results are raw path bytes with no color,
  escaping, or encoding conversion. Records end in LF by default or NUL with
  `-0`; bytes from different result records never interleave, even across worker
  batches. Use `-0` for arbitrary filenames. Newline output can be ambiguous
  and can contain terminal control bytes.
- Ignore precedence is `.ignore`, repository `.gitignore`, `.git/info/exclude`,
  then the conventional global Git ignore file. Proper ancestors are included.
  Explicit roots bypass rules for themselves; children still obey them.
  Hidden children require `-H` even when a rule explicitly unignores them.
  After opening a directory, fixed-name probes load its current `.ignore` and
  repository `.gitignore` policy before selecting any descendant, even if those
  names would appear late during enumeration. The `.git` boundary probe uses
  non-following metadata. Ignore precedence and limits are unchanged.
  See [ignore and thread semantics](docs/IGNORE_THREADS.md) for boundaries and limits.
- An explicit `--max-depth` cutoff is successful. Without a requested cutoff,
  reaching a directory at depth 128 reports a recursion safety error rather
  than silently claiming a complete search.
- Directory names stream through `CuDir`: each cursor's name arena has one
  active `CuTemp`, rewound before the next entry, with a reusable 4096-byte block
  anchor. Names are borrowed until the next entry or cursor release. There is
  no directory-wide name vector, complete-list prerequisite, or sort.
- With `-j N` above 1, queued, running, DONE-but-unread jobs and the job currently
  being delivered together occupy at most N pending slots. Aggregate output
  payload is at most N MiB in 256 KiB credits, not N MiB per job. Each slot has
  one exclusive credit; remaining credits are shared. Returned shared credits
  can unblock any producer; the FIFO head retains its exclusive progress path.
  Unread jobs retain their slots, and credits remain pinned through output I/O
  and cancellation until safe return or worker join.
- Active traversal can retain up to 128 directory handles per worker, plus
  coordinator handles and temporary ignore/policy handles. Cursor name/temp
  arenas, paths, rule state, stacks, bounded transport metadata, allocator slack,
  and the coordinator's
  one deferred-child lookahead per frame are outside the payload bound. This
  is not a total RSS cap. EMFILE/ENFILE are terminal resource failures; fynd
  does not raise descriptor limits.
- Filesystem metadata is not a snapshot. Intermediate symlink components and
  directory-replacement races can resolve outside a root. This walker is not
  a sandbox or a race-proof no-follow boundary.
- Directory-close errors are not exposed by the current c-utils API. Native
  Windows, unusual integer-width platforms, and race-proof traversal are unverified.

Default serial traversal and explicit `-j1` create no thread, mutex, or condition
primitives. Larger `-j` values use coarse streaming subtree workers; workers
never enqueue descendants. Only the coordinator writes caller streams. Whole
jobs drain through the existing FIFO transport, which can block behind its
head; this is an internal delivery detail, not a public DFS or arrival-order
guarantee, nor a promise of identical serial and threaded order. A single child
prefix stays on the coordinator until a useful fork; its parent cursor closes
before deferred recursion. Pending work drains, or cancels and joins, before
borrowed rules and paths leave scope. Failed or unsupported requested startup
returns 1 without silent fallback. Different resource costs and races can change
failure prefixes. No speedup is promised.

See [the original design](docs/DESIGN.md) and
[ignore/thread extension](docs/IGNORE_THREADS.md) for ownership and failures.
Their historical sorted-listing contracts are superseded by this candidate's
streaming contract above. Pattern and path handling are byte-oriented rather
than locale-aware.

## Development

```sh
make test
make cli-test
```

`make test` runs C boundary, allocator, stream, ignore, and thread tests,
including a barrier-based single-root overlap check. Run it under an external
timeout when testing scheduler changes. `make cli-test` drives the executable
against independent filesystem and ignore fixtures, with optional fd/Git subset
oracles. Default-stream oracles compare exact multisets with multiplicity and
separately enforce root-group order, not sorted DFS bytes. The former `test_sort`,
`test_listing`, `test_listing_worker`, and `sem_sort`/sorting fuzz tests are retired
historical contracts, not supported ordering APIs; streaming sensors replace them.
There are six active C test programs and 34 benchmark tests. Python 3 is required;
external tools are never installed automatically.

For a separate compiler or build profile, use a fresh build directory:

```sh
make CC=clang BUILD=build-clang all test cli-test
```

### Full verification

```sh
make verify
```

This optional target needs the Dependable C checker toolchain and an already
provisioned `dcheck-tools` profile. It defaults to
`~/projects/dependable-c-agent-toolchain/bin/dcheck`; set the `DCHECK`
environment variable to an absolute executable path to override it.
It does not install tools or change runner configuration.

Verification creates a 3 GiB systemd user scope with no swap, nice 15, and
core dumps disabled. There is no uncapped fallback. It copies the working
source into an isolated directory and extracts the pinned c-utils revision,
then runs native tests, seeded CLI comparisons, and the deep checker profile.
Artifacts and source hashes remain under `build/verification/run.*`.

The checker profile includes compiler, sanitizer, coverage, bounded semantics,
fuzzing, and Valgrind checks. A passing bounded harness is not a whole-program
filesystem proof. See [verification evidence](docs/VERIFICATION.md) for actual
commands, results, skips, and limitations. Full qualification requires every
unchanged required profile and the per-module coverage floor. Native timings
do not establish that pass.

### Serial benchmarks

```sh
make bench-test
make bench
make bench BENCH_ARGS='--quick --repeats 2'
```

The benchmark uses an existing `build/fynd`, GNU find, GNU time, and optional
fd. It runs under the same resource caps, validates selected-file multisets before
and during timing, and measures warm wide, deep, and mixed trees. fd uses one
configured search worker. No cache dropping or timing-ratio gate is used.
See [the benchmark procedure](bench/README.md) and [recorded baseline](docs/BENCHMARKS.md).

The native candidate comparison uses two matched warm waves, with serial fynd
versus fd's automatic threading as the primary flat-million-file comparison,
including file and pipe sinks, first-byte latency, and memory. This is workload-
and host-specific evidence, not a universal fd win or an isolated cold-cache
promise. Original tiny/quiet regression flags remain recorded. A follow-up of
two waves, 36 repetitions and 12 configurations, with the self-alias cleared,
does not prove zero regression or that earlier differences were merely noise.
The full qualification receipt is separate from these performance measurements.

## Research and next steps

The original research agenda is tracked in [docs/RESEARCH.md](docs/RESEARCH.md).
Further work includes Git config/worktree fidelity, glob or regex matching, additional
metadata filters, platform coverage, license selection, name-collision checks,
and broader performance comparisons. Threaded traversal is opt-in; benchmark it
on your workloads rather than assuming more workers help.
