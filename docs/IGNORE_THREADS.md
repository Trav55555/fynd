# Ignore rules and threaded traversal

Historical ordered-listing design. [Qualified unordered streaming](STREAMING.md)
now replaces sorted DFS and full-listing discard with entry streaming and
retained partial results. Root groups, selection and ignore precedence remain;
shared payload credits and FIFO whole-task transport still protect records.
Source/coverage counts below refer to this earlier acceptance, not today's files.

This extends the first-slice contract. Literal selection remains unchanged.
Serial is the default; `-j N`, `-jN`, or `--threads N` requests 1 through 32
workers. One uses the serial path without initializing thread primitives.
Larger values use actual CuThread workers plus the coordinator, not subprocesses.
All options still precede the pattern operand. Invalid counts return status 2;
requested initialization failure or unsupported threading returns status 1,
without silent serial fallback. Help/version alone never start workers.

## Output and worker authority

The coordinator retains root order, duplicates, sorted depth-first preorder,
caller stdout/stderr, and final status. Successful threaded output is
byte-identical to serial output on a stable filesystem. Workers run the same
walker on sibling subtrees, including ignore loading, selection, and result
preparation. They never access caller streams or mutable coordinator arenas.
Multithreading does not strengthen filesystem race or symlink guarantees.

The coordinator collects and sorts a complete directory, loads its rules, and
checks hidden/ignore/depth eligibility before submitting children. Selection
filters do not prune traversal. A directory with two or more eligible descending
children opens one sibling window. A single-child prefix stays on the coordinator
until a useful fork. Workers never submit descendants: each task runs a serial
subtree walk, and the window finishes before the coordinator leaves its parent.
A skewed delegated subtree cannot use idle workers to repartition itself.

## Jobs, output, and memory

At most N jobs globally may be queued, running, or DONE but not fully consumed.
The currently drained job still holds its token. Submission and worker claiming
follow sibling order. The coordinator consumes only the FIFO head, continuously
while it runs, then refunds its token after DONE and complete byte delivery.
A monotonic window cursor considers each remaining sibling once. Later jobs
cannot occupy every worker ahead of a required task that has not started.

Each stable task slot owns a seed arena. The pool owns N slabs of 1 MiB output
payload, divided into 4N typed 256 KiB batch credits, at most 128 controls.
Each slot exclusively reserves one credit; the remaining 3N credits are shared.
A task may borrow more than the former dedicated 1 MiB, but aggregate payload
capacity remains N MiB. Workers use separate traversal arenas and synchronous,
owned directory listings. No arena/vector is shallow-moved. The parent ignore
chain is immutable and borrowed until every dependent worker has finished.
Worker traversal storage is released before DONE; completed jobs retain only
seed storage and unread bytes.

Each batch contains raw bytes for one output channel and an explicit flush flag.
Channel changes, full buffers, and diagnostic flushes publish per-task FIFO
batches. Producers fill private credits and claim replacements only when needed.
Published bytes and channel/flush fields remain immutable through delivery;
queue links change only under the mutex. The coordinator pins the IO credit
while writing outside scheduler locks, then returns it and wakes all space
waiters. Final partial and exactly full tails publish with DONE without acquiring
another credit or waiting for final acknowledgement. Long paths and escaped
diagnostics split across batches. Stdout retains its final-flush policy;
diagnostics flush stderr. No spool files, unbounded result queue, whole-search
sorting, or unordered output is used.

The pool has one mutex and separate work, publication, and credit-space
conditions. Every shared wait predicate is checked under the mutex. Cancellation
wakes all three groups and pins outstanding credits until workers join. The head
can progress through its reserved credit even when future jobs hold every shared
credit, but ordered output still permits head-of-line stalls.

The old listing-prefetch memory formula no longer applies. At a fork at depth f,
the coordinator retains f+1 listings; each of k running workers can retain at
most 127-f more. Live listings are bounded by f+1+k*(127-f), k <= N, and globally
by max(128, 1+127*N). Their widths are not byte-bounded. N MiB output payload,
bounded batch metadata, 32 task controls, seed arenas, actor paths/rules/scratch,
allocator slack, and thread stacks are additional. This is a job/output-staging
bound, not a total RSS cap.

Job errors are interpreted in traversal order when consumed. Unused speculative
errors are not separately diagnosed. Different resource costs and filesystem
races can change failure observations and partial prefixes; error-prefix parity
is not promised. No partial listing publishes children. Ordinary filesystem
failures continue independent work; observed resource/output failures stop it.

Cancellation marks queued jobs terminal and wakes blocked producers. Active
workers check stop at directory/entry boundaries and credit waits; synchronous
filesystem calls, listing collection, sorting, and source reads are not forcibly
interrupted. On stop, the coordinator joins workers before unwinding borrowed
parent rule/listing scopes. Every successful start is joined exactly once before
synchronization destruction and pool-arena release. Startup tracks each acquired
resource and initializes all contexts before starting behind a ready/stop gate.
Never destroy a running arena to cancel it.

## Ignore policy

Only `.ignore`, Git sources, and the conventional global Git ignore file are
recognized. No `.rgignore`, `.fdignore`, or new finder-specific file is added.
High to low precedence is:

1. `.ignore`, at any directory including proper ancestors.
2. `.gitignore`, within the nearest detected repository.
3. That repository's `info/exclude`, in its Git common directory: `.git`
   itself, or for a `.git` file (linked worktrees, submodules) the directory
   its `gitdir:` line names, or that directory's `commondir` when present.
   A relative path in either is taken from the file's own directory, and a
   `.git` file without a `gitdir:` line supplies no exclude source.
4. `$XDG_CONFIG_HOME/git/ignore` for nonempty XDG_CONFIG_HOME, otherwise
   `$HOME/.config/git/ignore` for nonempty HOME.

Within each source class, the deepest matching file wins, then its last matching
line. Git sources require a repository marker and stop at the nearest nested
repository. `.ignore` remains active across repository boundaries. Global rules
use repository-relative paths, not the command's root spelling. A `.git` marker
file establishes a boundary, but its contents/worktree gitdir are not resolved.
Git configuration, including core.excludesFile, is not parsed.

Explicit roots bypass ignore and hidden exclusion for themselves. Proper
ancestor rules and root-directory rules still govern their children. A pruned
directory is never entered, so a later child negation cannot rescue its contents.
Hidden visibility remains independent: negation does not expose dot-prefixed
children without `-H`. Selection and hidden filters never conceal rule-file or
repository-marker metadata needed to decide other entries.

`--no-ignore` bypasses all ignore discovery, source reads, and matching. It does
not imply `-H`. Ancestor/source work is unnecessary for leaf roots or directories
at an explicit depth cutoff. No fixed 4096-byte matching/cwd buffer or silent
long-path omission is permitted. Display paths retain the existing spelling. Native matching origins use
physical root ancestry from dynamically allocated realpath, so static
symlink/.. spellings read the actual directory's rules. WASI/Cerberus use a
capability-limited lexical fallback; physical ancestry there is unqualified.

## Rule syntax and failures

Ignore patterns remain case-sensitive; `-i` only affects literal selection and
extensions. Reuse cgrep's private byte matcher/parser where sound: comments, CRLF, escaped
initial `#`/`!`, trailing spaces, negation, leading/trailing slashes, `*`, `?`,
classes/ranges, and whole-component `**`. Preserve its tested malformed-class
nonmatching behavior rather than claiming complete Git pattern compatibility.
The matcher allows 1,000,000 charged steps per candidate and component/recursive
depth through 256. Each source is limited to 16 MiB. Exhaustion reports E2BIG,
never a silent nonmatch. Embedded NUL is a source error. Parser tests use exact-length, non-terminated input buffers.

A necessary-first-literal-byte filter skips only impossible rules. Anchored
rules compare directly; unanchored rules lazily build a charged local set of
component starts. Unknown initial glob operators fall back unchanged. The parser,
precedence, and fixed limits do not change. Fewer actual operations can complete
within the same limit; identical old/new budget exhaustion is not promised.
See [the performance pass](PERFORMANCE_PASS.md) for differential tests and controls.

Only missing sources, ENOENT or ENOTDIR, mean empty rules. Other read/probe errors
produce a diagnostic and status 1, skipping the affected directory's children
or root traversal; unrelated work can continue. Allocation/resource failures
are terminal. Nonregular rule sources must not block the walker on a FIFO or
device. Source symlinks are followed only to regular files. A nonblocking open
and a second type check prevent a replacement FIFO from blocking, but do not
make source opening race-proof or prevent all device-open side effects.
Source-close errors are not promised. A WASI capability boundary may
limit ancestor inspection and must be documented and tested rather than
silently disabling native ignore behavior.

## Private ignore boundary

`src/ignore.inc` is included after the application's checked path helper. The
caller supplies a current CuArena scope. All success outputs and source text
remain valid until that scope ends; failure outputs are unchanged. Frames are
immutable parent-linked records, never a parent CuVec mutated from child scope.
The source file defines these private types and static operations:

```c
typedef struct IgnoreFrame IgnoreFrame;
typedef struct IgnoreError {
    int number;
    const char *path;
} IgnoreError;

static int ignore_seed(CuArena *a, const char *root,
    const IgnoreFrame **out, IgnoreError *error);
static unsigned ignore_listing_bit(CuStr name);
static int ignore_enter(CuArena *a, const IgnoreFrame *parent,
    const char *directory, unsigned listed,
    const IgnoreFrame **out, IgnoreError *error);
static int ignore_verdict(CuArena *a, const IgnoreFrame *frame,
    const char *path, int is_dir, int *verdict, IgnoreError *error);
```

Seed prepares proper ancestors and a nonnull origin context, not root-directory
rules. Enter adds rules from the current complete listing. Verdict returns a
successful out value of 1 ignored, -1 explicitly unignored, or 0 unmatched.
All three status-returning functions return 0 on failure and fill IgnoreError;
ENOMEM, E2BIG, EMFILE, and ENFILE are terminal resource failures. Error.path must
remain available until the caller emits its diagnostic. No-ignore callers
must not call these operations. The public fynd_main and caller-owned-arena
private runner signatures remain unchanged.

## Design choice and verification

The first threaded design used listing prefetch. Measurements showed little
useful parallelism on branching trees; dispatching more tiny listings made
latency worse. Coarse sibling windows retain ordered output and one shared walker
while moving traversal and filtering onto workers. They accept a larger live
ancestor-memory allowance and limited skew handling. A resumable continuation
walker was rejected for now because its ownership and scheduling were more complex.
See [the coarse traversal experiment](COARSE_TRAVERSAL.md) for the initial results.
The later [shared-credit and ignore pass](PERFORMANCE_PASS.md) replaces dedicated
mailboxes without changing sibling scheduling or the aggregate payload bound.

Qualification includes a real two-directory overlap barrier, exact serial/threaded
bytes, DONE-inclusive budget assertions, exactly full final tails, and future jobs
holding every shared credit while the head progresses. The suites also cover
startup/allocation failures, ordinary versus terminal worker errors, and
coordinator-triggered cancellation before borrowed CuTemp scopes end.
Existing ignore, raw-byte, long-path, compiler, analyzer,
sanitizer, platform, coverage, semantics, fuzz, and behavioral sensitivity gates
remain required under unchanged resource caps.
