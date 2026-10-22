# First-slice contract

Historical first-slice contract. [Unordered streaming](STREAMING.md) now
supersedes the sorted-DFS, whole-listing storage, and partial-listing discard
rules below. Selection, root ordering and duplicates, raw record encoding,
borrowed-stream handling, and status classes remain. The earlier
[ignore/thread design](IGNORE_THREADS.md) is also a historical extension;
consult the streaming report and README for the current walker.

fynd is a serial, read-only POSIX file finder. The first implementation is
not a complete fd or GNU find replacement. Regex, ignore files, parallelism,
file mutation, and command execution are outside this slice.

## CLI

```text
fynd [OPTIONS] [--] [PATTERN [ROOT ...]]
```

Options precede the first operand. The first operand is always a literal
basename substring, even if it names an existing directory. No operands
means an empty pattern and root `.`. Remaining operands are independent roots
in argument order. An empty pattern permits a root-only search. Empty roots
are usage errors. `--` ends option recognition. No short-option clusters or
`--key=value` syntax in this slice.

- `-i`, `--ignore-case`: ASCII case folding for names and extensions.
- `-t`, `--type`: one of `f`, `d`, `l`, `o`. Repeated values form a union.
- `-e`, `--extension`: nonempty final suffix without a dot or slash.
  Repeated values form a union. `.profile` and `a.` have no extension;
  `.config.json` has `json`. Applies to every kind, not only regular files.
- `-H`, `--hidden`: include and descend into discovered dot-prefixed names.
- `-0`, `--print0`: NUL records instead of newline records.
- `--max-depth N`: explicit selection depth, 0 through 128. Root is depth 0.
  Objects at the limit can be selected, but their directories are not opened.
- `-h`, `--help` and `--version`: sole-argument informational invocations.
  Print to stdout and return success, or runtime failure if output fails.

Boolean flags are idempotent. Repeated depth limits use the last valid
value. Type and extension filters are unions internally and ANDed with the
name predicate. No filter except hidden pruning and depth controls descent.
An unmatched directory must still be searched.

A private Query record owns the validated pattern view, type mask,
extension views, roots, ASCII mode, hidden policy, record delimiter, and
requested depth. Views borrow argv only until the invocation returns.
The default requested depth is unbounded; a separate recursion safety
boundary at directory depth 128 reports an incomplete search with status 1
if traversal would continue. An explicit --max-depth cutoff is successful.
Neither limit silently truncates a path.

## Traversal and output

Every root is eligible for output before descendants, including explicit
hidden paths, files, broken symlinks, and non-regular objects. Explicit roots
bypass hidden pruning for themselves, not their descendants. Root spellings
lose trailing slashes except for `/`; other lexical spelling is retained.
This prevents a final symlink with `link/` spelling from forcing a follow.
Basename is the final component. `/`, `.`, and `..` retain those names.

Roots are not deduplicated. Each directory's entries sort in ascending
unsigned-byte order, then are visited depth first, before their children.
Paths retain the root prefix and use one added slash when needed. Raw path
bytes plus newline or NUL are the only result bytes. No escaping, color, or
locale conversion. Use NUL mode for arbitrary filenames; newline output
can be ambiguous and terminal-hostile.

Classify roots with lstat before opendir. Discovered and explicit static
symlinks are selectable leaves, never deliberately descended. Intermediate
symlink components and replacement races can still resolve outside a root.
This path-based CuDir walker is not a sandbox, race-proof no-follow walker,
or filesystem snapshot. Read-only refers to filesystem operations, not to
where a caller redirects stdout.

## Status and failure

- 0: requested search completed, including zero matches.
- 1: observed filesystem, allocation, resource, or output failure.
- 2: invalid invocation, before traversal or result output. Failure to write
  its diagnostic or flush output can instead produce status 1.

A valid invocation with an existing output-stream error returns 1 without
traversing. Stream error indicators are never cleared by the invocation.

Filesystem errors skip the failed root/subtree and continue independent
work. An incomplete directory listing is discarded before any children from
that listing are visited. A root may already have been emitted. Resource
and observed output failures stop all traversal and unwind. Partial results
are not rolled back. Diagnostics escape unusual path bytes without arena
allocation. Failure to write a diagnostic is terminal.

Check the final output flush. Never close caller streams. Do not install
signal handlers in the invocation seam or CLI; POSIX SIGPIPE may terminate
the executable instead of returning status 1. Buffered errors may only be
observed at final flush. CuDir close does not expose close errors, so the
application does not promise directory-close error detection.

## Ownership and module shape

Public interface: `int fynd_main(int argc, char **argv, FILE *out, FILE *err)`.
It borrows argv and distinct writable streams for the call, modifies no
argv bytes, and retains nothing after return. Concurrent calls must not
share mutable argv or streams. Internal options, containers, and allocators
are not public API.

- `cmd/main.c`: executable entry.
- `include/fynd.h`: the one public invocation contract.
- `src/fynd.c`: options, pure matching/path operations, POSIX root metadata,
  sorted traversal, diagnostics, and output policy.
- `tests/`: C boundary/fault tests and an independent Python CLI oracle.

One stable CuArena belongs to the invocation. Query vectors bind to its base
scope. A directory CuTemp owns copied names and a CuVec of entries. Close
the directory before sorting. Freeze its vector before opening child temp
scopes; parent containers are never mutated inside child scopes. A child
scope owns its joined path and all completed subtree storage is reclaimed
before the next sibling. Active ancestor widths and paths determine memory,
not the total number of completed directories. Sorting a wide directory can
still exhaust memory.

The private runner accepts a caller-owned CuArena so tests can enable debug
allocations, fail_at, and byte limits before parsing. The public seam
initializes/releases its own arena. Test private operations by source
inclusion or test-only exposure, not a larger installed API. Do not replace
malloc or weaken error returns to aid tests.

## Synthesis decision

Candidate A, bounded recursion with CuTemp, is the base. Candidate B used a
linked explicit stack with stable per-frame arenas. Both hide the whole
invocation behind one public function. The explicit stack lost because it
adds node allocation, scratch arenas, and frame lifecycle without a current
need for stack-independent traversal.

Adapted from B: options stop at the first operand, incomplete listings are
discarded, and errors remain distinct from successful depth cutoffs. Added
familiar short aliases and an observable safety error instead of silently
stopping at a default depth. FIFO, repeated-invocation, and broken-root-link
fixtures remain part of the acceptance cases. SIGPIPE follows normal POSIX
process policy instead of changing a caller's signal disposition.

## Verification requirements

Use an independent Python lstat/listdir oracle for exact ordered byte output,
statuses, multiple roots, filtering versus pruning, hidden explicit roots,
links, FIFO, case folding, extensions, unusual bytes, and depth boundaries.
C tests cover malformed args, helper boundaries, arena fail-at sweeps, byte
limits, write/flush failures, listing errors, and successful subsequent calls.
Do not generate expected output from the production matcher.

Use strict C89, GCC and Clang, optimized builds, sanitizer builds, static
analysis, bounded fuzzing of pure matcher/path/parser operations, and narrow
mutation sensitivity checks. Resource-heavy checks run sequentially with
3 GiB, no swap, nice 15, and core dumps disabled. No claim of whole-program
proof or performance parity follows from green checks.
