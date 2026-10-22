# Review the retained streaming implementation

Review the current source qualified by `run.z7sAXq`, not the private retirement
candidate. The active executable remains the selected `run.vbgYhn` binary.
The [decision checkpoint](DECISION_CHECKPOINT.md) consolidates the selection,
rejected alternatives and unresolved limits. This review does not authorize a
commit, release, new experiment or performance-policy change.

## Verify the local checkpoint

This workspace contains ignored local evidence. A fresh clone does not contain
the linked archives. The command below checks existing files and receipts.
It refreshes only `build/review-checkpoint/manifest.json`. It does not build,
run fynd, rerun qualification, collect timings or modify historical evidence.

1. Open a terminal.
2. Run the checkpoint check:

```sh
	cd /home/travers/projects/fynd
	systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
	  nice -n 15 bash -c 'ulimit -c 0; python3 -I build/review-checkpoint/check.py; rc=$?; exit "$rc"'
```

3. Confirm `SOURCE_AND_RECEIPTS_VERIFIED` in the result.
4. Inspect `build/review-checkpoint/manifest.json` for file hashes, qualification
   status, binary identities and the exact scope of this check.

The active local `build/fynd` now matches the selected archived executable.
The checker requires their SHA256 hashes to match. Use either path for that
executable, but retain the archive as the qualification reference. Do not reproduce
against the mutable sibling c-utils checkout; the selected archived dependency
is the qualified input.

## Inspect the implementation

Review these areas in order. The local files must match their counterparts under
`build/pinned-build/candidate/build/verification/run.z7sAXq/source/fynd/`.

1. Check `include/fynd.h`, `cmd/main.c` and `src/fynd.c` for the public contract,
   argument/status behavior, root ordering and error paths.
2. Check `src/ignore.inc` for precedence, physical/display path separation and
   immutable policy lifetimes.
3. Check `src/listing.inc` for cursor ownership, expiring entry views, sticky
   errors and the policy-before-next rule in its caller.
4. Check `src/coarse.inc` for FIFO whole-task delivery, unread DONE accounting,
   output ownership, credit return and join-before-release behavior.
5. Check `Makefile`, `CUTILS_REVISION` and `ci/pinned_cutils.py` with its manifest
   for the strict C89 build, exact dependency snapshot and atomic output update.
   Check `tests/test_build.py` for fresh and current-target refusal paths.
   Native builds require Python 3's standard library, not checker tools.
6. Check `tests/test_fynd.c`, `test_ignore.c`, `test_parallel.c`, `test_batches.c`,
   `test_stream.c` and `test_cursor.c` under `tests/` for the corresponding
   invariants. Check `semantics/`, `fuzz/` and `tests/verify.sh` for the remaining
   qualification requirements and explicit optional-tool skips.
7. Read [the full benchmark's losses and limitations](STREAMING_FULL_BENCHMARK.md)
   alongside its wins. Keep instrumented, direct-CLI and historical wrapped
   timing endpoints separate.

Exclude `build/ready-retirement/candidate/` from the retained implementation.
Its tests and package gains do not make it release-qualified.

## Record findings before delivery

Use file paths and concrete counterexamples for findings. Separate implementation
bugs from measurement uncertainty or a proposed policy change. If review changes
implementation or qualification inputs, require full qualification before
retaining those changes.

Check `git status --short` when preparing a future commit. Most implementation
paths are untracked, so `git diff` does not show the complete review scope.
Keep unrelated files, ignored experiment directories and unretained candidates
out of any commit. This checkpoint neither stages files nor creates a commit.
