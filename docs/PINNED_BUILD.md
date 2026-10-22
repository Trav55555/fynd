# Pinned build safeguard

Normal builds now verify the c-utils source/header contents associated with
the revision in `CUTILS_REVISION` before compilation, including when the
requested executable is already current. The guard was introduced at
`eafa795805fa997e2db91f9182c4e0f86fa32fb4`; see [Pin 2](#pin-2) for the current
revision.

The previous Makefile could compile changed sibling files and overwrite an old
output on compiler failure. A regression demonstrated that behavior before the
fix. The retained build boundary rejects invalid inputs before invoking the
compiler, then compiles a private verified snapshot and atomically replaces the
output only on success. The existing live `build/fynd` was never rebuilt or
replaced during this work.

## What the guard checks

`ci/pinned_cutils.json` binds the whole c-utils `include/` and `src/` inventory
to byte sizes and SHA256 hashes, and lists the C sources fynd compiles. Schema
`fynd.cutils-pin/2` pins `include/cu.h` and 25 C sources and compiles 16 of them.
`ci/pinned_cutils.py` rejects missing, extra, changed, symlinked or nonregular
entries in those source/header directories, whether compiled or not.
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

## Pin 2

The pin moved to `7750e1c9855f6e1155772ae1b98f11830b21d074` for `cu_dir_info`,
which lets a directory's ignore probes (`.git`, `.ignore`, and `.gitignore`
inside a repository) use `fstatat` on the open listing instead of path lookups.
That revision also adds nine sources fynd does not use (JSON, HTTP, process,
socket, task and reader modules). Schema 1 compiled every pinned source, so the
manifest moved to schema 2: those files stay pinned and inventory-checked, and
`sources` names the 16 that are compiled, now including `src/cu_metadata.c`.
`include/cu.h` and `src/cu_append.c` also changed between the two revisions.

Qualified by `build/verification/run.z8rL06`, exit 0, on the tree merged with
#5: build guards (17 tests, including rejection of an unpinned, dropped or
unsorted `sources` entry), five C suites, three 500-case CLI seeds, the Git
differential, required deep checks, and all 31 named controls, including
`skip-gitignore-probe` and `ignore-probe-absence`. Coverage: main 94.43%,
ignore 96.30%, listing 91.95%, scheduler 96.66%.

Warm medians of 15 interleaved runs on one 12-CPU host, same output (329,177
records on the home projects tree):

| Tree | Threads | Before ms | After ms |
| --- | ---: | ---: | ---: |
| Home projects, ignore rules | 1 | 1,147 | 1,072 |
| Home projects, ignore rules | 12 | 171 | 160 |
| `/usr` | 1 | 362 | 342 |
| `/usr` | 12 | 51 | 47 |

## Excludes through a `.git` file

Reading `info/exclude` through a `.git` file (linked worktrees, submodules)
was qualified by `build/verification/run.eRMq0D`, exit 0, on c-utils
`7750e1c`: the same suites, the Git differential with linked worktrees and
separate git directories, and all 33 named controls, including the new
`ignore-gitfile` and `ignore-commondir`. Coverage: main 94.43%, ignore
97.35%, listing 91.95%, scheduler 96.92%.

## Ignore sources from the listing

Learning `.git`, `.ignore` and `.gitignore` from a listing that ends within
64 names, instead of probing every directory, was qualified by
`build/verification/run.HhIL98`, exit 0, on c-utils `7750e1c`: five C suites,
three 500-case CLI seeds, the Git differential, required deep checks, and all
35 named controls, including the new `probe-complete-listing` and
`unlisted-ignore-name`. Coverage: main 93.31%, ignore 97.41%, listing 91.95%,
scheduler 96.92%.

Warm medians of 15 runs on one 12-CPU host, same output:

| Tree | Threads | Before ms | After ms |
| --- | ---: | ---: | ---: |
| 9,331 directories, 37k files | 1 | 83.2 | 56.3 |
| 9,331 directories, 37k files | 12 | 14.7 | 11.8 |
| Home projects | 1 | 1,120 | 913 |
| Home projects | 12 | 206 | 179 |
| `/usr` | 1 | 350 | 292 |
| `/usr` | 12 | 60.3 | 55.5 |

## Hidden names re-included by ignore rules

Listing a hidden child without `-H` when a negation decides it was qualified by
`build/verification/run.jZaRNl`, exit 0, on c-utils `7750e1c`: five C suites,
three 500-case CLI seeds, the Git differential (now generating hidden names
and checked against `git check-ignore -v`), required deep checks, and all 37
named controls, including the new `hidden-ignores-negation` and
`drop-hidden-before-verdict`. Coverage: main 93.16%, ignore 97.41%, listing
91.95%, scheduler 96.92%. On the home projects tree, output now equals fd
10.3.0's (408,325 paths; 50 more than before), at unchanged speed.

## Threaded --sort

Running `--sort` on the thread scheduler, writing in walk order, was qualified
by `build/verification/run.l97tFG`, exit 0, on c-utils `7750e1c`: five C
suites (the scheduler suite now compares threaded `--sort` with `--sort -j1`
byte for byte on stdout and stderr), three 500-case CLI seeds, the Git
differential, required deep checks including fuzzing, ThreadSanitizer and
MemorySanitizer, and all 39 named controls, including the new
`unordered-sorted-records` and `child-output-after-parent`. Coverage: main
94.27%, ignore 97.41%, listing 91.95%, scheduler 96.65%.

Medians of nine runs on one 12-CPU host under outside load (load average
about 12), same output; "before" is the serial `--sort` of the previous
binary:

| Tree | Before ms | After ms | Unsorted ms |
| --- | ---: | ---: | ---: |
| 9,331 directories, 37k files | 79.3 | 46.5 | 22.6 |
| Home projects | 1,242 | 437 | 260 |
| `/usr` | 396 | 157 | 96 |

Peak RSS rises from 2.6 MB to 12.2 MB on the home tree and from 2.7 MB to
11.4 MB on `/usr`, for results held up to 1,024 directories ahead.
