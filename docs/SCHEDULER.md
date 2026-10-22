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
  longer gets a frame: `ignore_stream_enter` returns the parent. In a
  scheduled walk, a directory keeps its temporary scope open only when it
  created a frame that queued children borrow; everything else is released as
  in the serial walker. This elision is exact: an empty, non-repository frame
  cannot decide or scope any verdict.
- **Failures.** Allocation, thread-start and output failures are terminal:
  they set a shared stop flag, queued tasks are dropped unrun, running visits
  poll the flag as the serial walker does, and every thread is joined before
  shared memory is released. A late ordinary EIO stays nonterminal. A failed
  thread start returns 1 without a silent serial fallback. On WASI, whose
  c-utils thread backend reports ENOSYS, the automatic default is one thread.

## Contract changes

- The default changed from serial to the online CPU count. `-j N` counts
  all threads; previously it counted workers plus a coordinator.
- The fixed N MiB payload-credit bound is gone. Memory is now one 64 KiB record
  buffer per thread, one task per directory found but not yet listed, and
  ignore frames for directories with rules, held until their root finishes.
  An independent review measured about 500 bytes per rule-bearing directory:
  10,000 directories with a 20-rule `.gitignore` each peaked at 6.7 MB with
  `-j8`, against 1.7 MB serial. A root with a million such directories would
  hold about 0.5 GB until it finishes. Reference-counting frames per task would
  remove this; it is not done.
- A threaded walk holds one listing handle per thread, instead of up to 128
  per worker.
- Unchanged: root group order, whole records, unordered output within a root,
  ignore policy before any entry is read, partial results after a late EIO,
  and exit statuses.

## Verification

Qualified by `build/verification/run.XyeMPG`, exit 0:

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
