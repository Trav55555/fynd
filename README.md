# fynd

A planned file-finding tool in Dependable C, built on
[c-utils](https://github.com/Trav55555/c-utils).

**Status: research and scoping.** There is no executable or build system yet.
The commands below are design sketches, not supported commands.

## Direction

Find files and directories by name and selected metadata. Be a companion to
[cgrep](https://github.com/Trav55555/cgrep): cgrep searches file contents;
fynd would find the files themselves.

The initial goal is a small, read-only finder with explicit traversal,
matching, output, and error behavior. It is not a complete GNU find
expression engine or a claim of better performance than fd.

Possible command shapes:

```sh
fynd README .
fynd --type f --extension c src/
fynd --print0 .
```

Argument order, default roots, matching syntax, and option names remain open.

## Why c-utils

Use the application to test the library against real filesystem workloads.
Keep application-specific search and ignore policy out of the core library
unless a second consumer establishes a useful shared interface.

| Need | Existing c-utils building blocks |
| --- | --- |
| Enumerate directory entries | `CuDir` |
| Own paths and temporary traversal state | `CuArena`, `CuTemp` |
| View and construct paths | `CuStr`, `CuBuilder` |
| Maintain a traversal stack or sort entries | `CuVec` |
| Track visited objects or cached state, if needed | `CuMap` |
| Explore parallel traversal after a serial baseline | `CuThread`, `CuMutex`, `CuCond` |

These are candidate uses, not a requirement to use every container or add
parallelism. Investigate missing metadata and filesystem operations before
proposing new library APIs.

## Research TODOs

### CLI and compatibility

- [ ] Compare fd and GNU find for a small set of everyday file searches.
      Record which behaviors fynd intends to match and which it rejects.
- [ ] Choose literal, glob, or regex matching for the first slice. Specify
      whether patterns match the basename or the path relative to a root.
- [ ] Decide the default root, multiple-root behavior, case sensitivity,
      extension matching, and handling of patterns or paths beginning with `-`.
- [ ] Define exit statuses independently from cgrep. In particular, decide
      whether a successful search with no results returns 0 or 1.
- [ ] Check existing tools, packages, and executable names before distributing
      a binary named `fynd`. The repository name is not proof of availability.

### Traversal and ignore rules

- [ ] Specify discovered symlinks versus explicit symlink arguments, broken
      links, cycles, mount boundaries, and files that change during traversal.
- [ ] Define hidden-file defaults, ignore-file precedence, repository
      boundaries, explicit-root exceptions, and unreadable ignore files.
- [ ] Review cgrep's traversal and ignore code for reusable behavior and tests.
      Decide whether sharing needs a separate module rather than copying code
      or moving Git-specific policy into c-utils by default.
- [ ] Compare streaming results with sorted output. Measure the memory cost
      of sorting a large directory and define ordering across multiple roots.

### Filesystem and memory contracts

- [ ] Audit `CuDir` and the public c-utils API for available entry types,
      metadata, errors, and pointer lifetimes. List actual gaps first.
- [ ] Specify path construction and size-overflow checks. Test long paths
      without silently truncating them or inheriting cgrep's ancestor buffer.
- [ ] Choose an iterative or recursive walk with an explicit depth policy.
      Define where temporary allocations are reclaimed.
- [ ] Define behavior for permission failures, vanished entries, allocation
      failure, output failure, and partial results. Keep diagnostics separate
      from machine-readable results.
- [ ] Select the first supported platforms. Distinguish portable C89 code
      from platform-specific filesystem operations and unverified targets.

### Output and pipeline safety

- [ ] Define newline output and NUL-delimited output for arbitrary filenames,
      including spaces, tabs, newlines, leading dashes, and non-UTF-8 bytes.
- [ ] Research terminal-safe rendering separately from byte-preserving pipeline
      output. Do not make escaping or color change paths passed to other tools.
- [ ] Define broken-pipe and cancellation behavior. Stop work after an
      observed output failure rather than continuing an unnecessary walk.

### Correctness and performance evidence

- [ ] Build an independent fixture oracle with Python filesystem primitives
      or GNU find for the selected subset. Do not reuse the production walker
      or matcher as its own oracle.
- [ ] Add fixtures for deep and wide trees, empty directories, symlinks,
      hidden entries, ignore precedence, unusual filenames, and injected errors.
- [ ] Fuzz pure pattern and path-handling code with bounded inputs. Keep
      filesystem tests inside disposable fixtures.
- [ ] Plan strict C89 builds, allocation-failure tests, sanitizers, and checks
      for bounds, resource cleanup, and integer overflow.
- [ ] Validate output and status before timing against fd and GNU find.
      Record tool versions, fixture details, ordering, cache conditions, wall
      time, and peak memory. Make only claims supported by those workloads.
- [ ] Start serial. Add a bounded parallel prototype only if measurements
      identify a useful gain and ordering, errors, and cancellation stay testable.

### Packaging and project setup

- [ ] Choose a license compatible with the intended c-utils integration.
- [ ] Pin the tested c-utils revision and document a native build that does
      not require the optional Dependable C checker toolchain.
- [ ] Separate fast local checks from resource-capped full acceptance.
      Keep credentials, local agent state, and generated output outside Git.

## First implementation slice

After resolving the initial CLI and traversal questions:

1. Walk one explicit directory root serially, without following discovered
   symlinks. Keep file mutation and command execution out of scope.
2. Select entries with one documented name-matching mode.
3. Emit paths in one documented format and order, including a safe NUL mode.
4. Compare results and status with an independent oracle on disposable trees.
5. Test error handling and cleanup before expanding the feature set.

No daemon, file deletion, command execution, full find expression language,
or fastest-tool claim belongs in this first slice.

## Starting references

- [c-utils API and ownership model](https://github.com/Trav55555/c-utils)
- [cgrep traversal and ignore implementation](https://github.com/Trav55555/cgrep)
- [fd usage and defaults](https://github.com/sharkdp/fd)
- [GNU Findutils manual](https://www.gnu.org/software/findutils/manual/)
- [Git ignore-pattern rules](https://git-scm.com/docs/gitignore)
- [Dependable C](https://dependablec.org/)
