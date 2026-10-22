# Pinned build safeguard

Normal builds now verify the c-utils source/header contents associated with
`eafa795805fa997e2db91f9182c4e0f86fa32fb4` before compilation, including when the
requested executable is already current. The dependency revision did not change.

The previous Makefile could compile changed sibling files and overwrite an old
output on compiler failure. A regression demonstrated that behavior before the
fix. The retained build boundary rejects invalid inputs before invoking the
compiler, then compiles a private verified snapshot and atomically replaces the
output only on success. The existing live `build/fynd` was never rebuilt or
replaced during this work.

## What the guard checks

`ci/pinned_cutils.json` binds 16 consumed files to byte sizes and SHA256 hashes:
`include/cu.h` and 15 C sources. `ci/pinned_cutils.py` rejects missing, extra,
changed, symlinked or nonregular entries in those source/header directories.
It checks the manifest revision against `CUTILS_REVISION`, not Git HEAD.
Other checkout files are outside this native-build check.

Each compile hashes the same bounded bytes it copies into a unique private
snapshot. Only snapshot dependency paths reach the compiler. Changes to the
original dependency during compilation cannot alter those captured bytes.
Guard or compiler failure leaves an existing output intact. Valid current targets
are checked without forced recompilation. Normal completion cleans the temporary
snapshot; abrupt process termination can leave private staging debris.

Make retains its compiler, flags, application and test discovery interfaces.
Python 3's standard library is now an explicit native prerequisite. Native builds
need no Git metadata, dcheck, analyzer, network access or installed dependency.
Compiler/flag overrides, application sources and hostile same-user writes are not
an adversarial sandbox guarantee.

The current default sibling checkout is unpinned and is correctly refused.
A pinned checkout or Git-free archive can be supplied with `CUTILS`. For example,
this local verification build uses the archived dependency and separate output:

```sh
	make CUTILS="$PWD/build/pinned-build/candidate/build/verification/run.z7sAXq/source/c-utils" \
	  BUILD=build/pinned-native all
```

That archive is an ignored local artifact, not part of a fresh clone. The existing
full verification wrapper still exports the immutable pinned Git object into its
receipt; it never compiles the sibling working tree.

## Qualification and retention

Full qualification [`run.z7sAXq`](../build/pinned-build/candidate/build/verification/run.z7sAXq/)
passed before retention. It includes 17 build-boundary tests, six C suites,
6,656 CLI checks, 34 benchmark-helper tests, every required deep profile,
four semantics targets, four bounded fuzz targets and all 25 compile-valid
behavioral controls. Each C module remains above the unchanged 90% coverage floor.
Optional authority, regions and CBMC remain explicitly unconfigured.

The retained six paths are `Makefile`, `README.md`, `tests/verify.sh`,
`tests/test_build.py`, `ci/pinned_cutils.py` and `ci/pinned_cutils.json`.
All 105 selected source inputs match the qualification archive. Application C,
public headers, pin, runtime behavior and performance policy are unchanged.
The active executable remains the exact previously qualified `run.vbgYhn` bytes.
The newly built qualification executable was not promoted.

Post-retention checks exercised the Root Makefile with isolated outputs: valid
archive build, current-target validation without recompilation, bad-pin refusal
with old output preserved, and default sibling refusal. No performance campaign,
commit, global installation or publication occurred.

[Detailed receipt](../build/pinned-build/receipt.md) preserves the original RED,
regressions, reviewer counterevidence, complete qualification and live-byte checks.
[Review checkpoint](REVIEW_CHECKPOINT.md) distinguishes source qualification from
the unchanged live executable's qualification.
