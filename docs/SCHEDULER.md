# Shared-stack thread scheduler

Retained. Threaded traversal now shares work at directory granularity: every
thread pushes the subdirectories it finds onto one task stack, and any thread
may take them. This replaces the coordinator-only FIFO pool (`src/coarse.inc`).
fynd now defaults to one thread per online CPU, clamped to 1 through 32;
`-j1` remains the unchanged serial walker.

## Why the old design did not scale

The FIFO pool let only the coordinator hand out work. Workers ran whole subtrees
and could not split them, and output drained strictly in task order, so the
coordinator blocked on the oldest task before it could continue walking.
Five scheduler variants that kept that shape were rejected for regressions on
small trees ([frontier](FRONTIER_TRAVERSAL_EXPERIMENT.md),
[skew](SKEW_TRAVERSAL_EXPERIMENT.md), [scheduling](SCHEDULING_EXPERIMENT.md),
[frontier cost](FRONTIER_COST.md), [contention](FRONTIER_CONTENTION.md)).
Against fd's automatic threading, `-j8` stayed 2-5x slower on large trees.

## Design

- **Threads.** `-j N` means N threads in total. The coordinator is worker 0.
  It walks each directory root itself and starts the other N-1 threads only
  when it pushes the first subdirectory, so a root without subdirectories
  starts none.
- **Tasks.** `visit()` lists one directory. When a scheduler is active, each
  descending child directory becomes a task (path, depth, borrowed ignore
  frame) on a LIFO stack under one mutex, instead of a recursive call. Cutoff
  and depth-128 directories are still visited in place, so `--max-depth` and
  the recursion safety limit behave as in the serial walker.
- **Output.** Each thread buffers whole records (64 KiB) and writes them to
  the caller's stream under an output mutex. Records never interleave.
  Diagnostics are written whole under the same mutex. A run reports
  `output failure` once, however many threads observe it.
- **Root barrier.** A root ends when no task is queued or running. Every
  started thread then writes its buffer and releases its arena before the
  coordinator moves to the next root. Root groups therefore stay in argument
  order, and no arena that a queued task borrows from is released early.
- **Ignore frames.** A directory without rule files or a `.git` marker no
  longer gets a frame: `ignore_stream_enter` returns the parent. This elision
  is exact: an empty, non-repository frame cannot decide or scope any verdict.
  In a scheduled walk, a frame that is kept lives in a *hold*: a small arena
  counted by the visit that made it, by every queued or running task that
  borrows it, and by each child hold whose frame chains to it. When the count
  reaches zero the hold is freed and its parent's count drops, so a frame
  lives only as long as work in its subtree. Holds are collected under the
  scheduler mutex and freed after it is released. Directories that load no
  rules roll back a temporary scope in a per-worker spare hold and allocate
  nothing. The root's seed frames live in worker 0's arena until the barrier,
  which also checks that no hold is still live.
- **Failures.** Allocation, thread-start and output failures are terminal:
  they set a shared stop flag, queued tasks are dropped unrun, running visits
  poll the flag as the serial walker does, and every thread is joined before
  shared memory is released. A late ordinary EIO stays nonterminal. A failed
  thread start returns 1 without a silent serial fallback. On WASI, whose
  c-utils thread backend reports ENOSYS, the automatic default is one thread.

## Ordered walks (`--sort`)

`--sort` uses the same scheduler and the same thread count; it no longer
forces `-j1`, and `--sort -j N` is valid. The walk stays exactly the serial
`--sort` walk on both streams:

- **Deferred results.** A task still does one directory's whole serial work:
  list it, sort it, enter its ignore policy and decide each child. It does
  not write. Its records go into its own result, with an event where each
  descending child's output belongs and where each failure occurred.
- **One writer.** The coordinator drains results depth first: a task's bytes
  up to an event, then that child's output, or that failure reported as the
  serial walk would. A failure takes effect only when the writer reaches it,
  so helpers never stop the walk early, and a terminal failure ends output
  exactly where the serial walk would.
- **Walk order.** Children are queued when their parent finishes, on a heap
  ordered by depth-first walk position. Helpers take the first; the writer
  takes and runs a task it needs that no helper has started.
- **Lookahead.** Helpers run at most 1,024 tasks the writer has not reached
  (`SCHED_AHEAD`). The limit bounds memory; at 256, parents that finish late
  left their subtrees to the writer and halved the speed-up.
- **Frames.** A task keeps the hold its children borrow until it has queued
  them, so a frame never dies between a parent's visit and its children's.

`tests/test_sched.c` compares threaded `--sort` with `--sort -j1` byte for
byte, stdout and stderr, across six flag sets, four thread counts and the
default, repeated roots, a late EIO, the depth safety limit and borrowed
rule frames, with a two-task lookahead so the writer often runs tasks itself.
Output failure must match exactly, and allocation and thread-start failures
must leave stdout an exact prefix of the full walk.

## Contract changes

- The default changed from serial to the online CPU count. `-j N` counts
  all threads; previously it counted workers plus a coordinator.
- The fixed N MiB payload-credit bound is gone. Memory is now one 64 KiB record
  buffer per thread, one task per directory found but not yet listed, and the
  ignore frames that live work still borrows: those of directories being
  listed, their ancestors with rules, and the parents of queued directories.
  The first qualified version held every rule-bearing frame until its root
  finished; on 10,000 directories with a 20-rule `.gitignore` each it peaked at
  22 MB at the default thread count. Reference-counted holds bring that to
  3.7 MB, the same as serial, and the walk got slightly faster (30-31 ms
  against 33-34 ms).
- A threaded walk holds one listing handle per thread, instead of up to 128
  per worker.
- Unchanged: root group order, whole records, unordered output within a root,
  ignore policy before any entry is read, partial results after a late EIO,
  and exit statuses.

## Verification

Reference-counted holds are qualified by `build/verification/run.bhkWaq`, exit 0:
all 27 named controls reject, including `retain-visit-hold`; coverage main
94.19%, ignore 95.96%, listing 91.95%, scheduler 96.92%; combined 95.39%.
`tests/test_sched.c` also checks that at most one hold per running visit plus
the root's own is ever live (5 at `-j4`) on 200 rule-bearing siblings.

The scheduler itself was first qualified by `build/verification/run.XyeMPG`, exit 0:

- Build guards; five C suites; three 500-case CLI seeds (6,324 checks) and 332
  ignore CLI checks; required deep checks (sanitizers, MSan, TSan, Valgrind,
  fuzzing, WASI, i386). Authority, regions and CBMC remain optional skips.
- All 26 named behavioral controls reject, including scheduler controls for
  lost barrier flushes, a missing output-failure report, a lost barrier write
  failure, a serial default, suppressed thread start and disabled frame
  elision.
- Coverage: main 95.04%, ignore 95.96%, listing 91.95%, scheduler 97.99%;
  combined 95.84%.

`tests/test_sched.c` compares every threaded run with `-j1` on status, record
multiset and diagnostic multiset, for six flag sets at 2, 3, 8 and 32 threads
and the default. It also covers root-group order with duplicate roots, threads
first starting on a later root, the depth-128 limit and `--max-depth 128`,
exact `/dev/full` diagnostics during the walk and at the barrier, stderr
failure, task allocation failure, and thread start failure before the walk and
for the first, second or third thread. It uses a 64-byte record buffer to force
constant flushing and the oversized-record path. ThreadSanitizer reported
nothing across repeated runs of this suite and of the CLI at `-j2`, `-j32` and
the default. A Git differential of 2,000 random rule sets and trees matched
`git ls-files --others --exclude-standard` serially, at `-j8` and at the
default. An independent review traced the wakeup, barrier, stop and scope
protocols and found no defect.

## Performance

Warm medians of nine runs on a 12-CPU desktop (load about 3), one host,
descriptive rather than significance claims. "Old" is the previous release
binary at its serial default; "new" uses its default of 12 threads; fd uses its
own default threading.

| Tree | Old ms | Old -j8 ms | New ms | fd ms |
| --- | ---: | ---: | ---: | ---: |
| Tiny source directory | 0.6 | - | 0.6 | 2.8 |
| Python dist-packages, 7k entries | 10.8 | - | 2.6 | 6.3 |
| One 100k-file directory plus 7 small | 31.6 | - | 31.8 | 51.1 |
| 9,331 directories, 4 files each | 59.3 | 14.4 | 10.4 | 16.6 |
| Worktree repository, 82k results, ignore rules | 121.1 | - | 20.3 | 37.3 |
| Home projects, 404k results, ignore rules | 1,195.5 | 965.5 | 200.9 | 258.6 |
| `/usr`, 300k entries, `--no-ignore` | 287.6 | 143.3 | 44.4 | 68.7 |

`-j1` stays the serial walker: 1,199.1 ms against 1,195.5 ms on the home tree.
A single huge directory cannot be split, so it runs at serial speed. CPU time
is higher than serial (1,735 ms against 1,192 ms on the home tree) but lower
than fd (2,198 ms). Peak RSS on the home tree with ignore rules is 5.7 MB new,
3.7 MB old and 23.7 MB fd; on `/usr` it is 3.5 MB new and 21.8 MB fd.
