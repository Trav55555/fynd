#!/usr/bin/env python3
"""Native build-boundary tests; all builds and mutations are disposable.

Usage: python3 -I tests/test_build.py --project PATH --cutils PATH
       [--build-root PATH] [unittest selections/options]

The dependency must be an export of the pinned revision, not a dirty checkout.
No Git command, benchmark, or live-project build is used by this suite.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest


REVISION = "7750e1c9855f6e1155772ae1b98f11830b21d074"
# Deliberately independent of the candidate manifest (and available for before/).
PINNED = {
    "include/cu.h": ("4b182eb14cc9cf33b1b7e5dc3b711f99cf55312c13c5bf44444098bbde21ae03", 78175),
    "src/cu_append.c": ("49075f2382c53c67673be8ae543e3df96985c35c5f5789f7c4bd60a824d9ce59", 6670),
    "src/cu_arena.c": ("90aa4b804e6e38900b1ddec1a858c82909f41936560a623e2f24d6365297c8d7", 12263),
    "src/cu_base64.c": ("caee0eee8af47ae4747a20605bccae1a09dd626c93c645ff56333374b4f4ea2b", 2859),
    "src/cu_bytes.c": ("57e0faf943e2f618cbbcc0b73dec4fc1346486ae71aeb9e3d39b19ae5afb53d8", 4919),
    "src/cu_dir.c": ("254f8afb5581ecae461a5145ce20a397ce0ead3dfacd68e70e1a4a5461921317", 3659),
    "src/cu_file.c": ("902f53ebac1e2e9e6da78afa0d26668ed440310c7f841061c21f9cf14c1360ff", 3014),
    "src/cu_fmt.c": ("a61c5c3ea72f0bbc2ca7dd165ae5ec51293458f5eabcc5531959417a7eae7828", 8023),
    "src/cu_http.c": ("968731a330737e5226c4f5d49ee88399110dd8386d078eab1f2bbab2ca1e7509", 5917),
    "src/cu_http_response.c": ("7c0107870733d458c8f95a5b4ea439951776a7be59dd98c64392b80d522a1935", 6977),
    "src/cu_input.c": ("9b5503af37a45c721a6837d79207da5df04a6eabc17211ed7823cdbe579698aa", 2296),
    "src/cu_json.c": ("40025fe498f0a17e18224a3527ee4c7f3fc55f2d305c75ee6ec6ef9d6ba62620", 22916),
    "src/cu_map.c": ("55297d4a73aeb3a73f19d9b2b753d3aa3950d7b4cb97d44f84d3fd75ddb92b07", 5071),
    "src/cu_metadata.c": ("f5f5f8624aec7840b1b51b7557dfcb0f293a2c8c4a7b19ab72ea831d604dcc5c", 4057),
    "src/cu_plex.c": ("d74b8927bc441a3cec8e4060a0c4c8cf6f945bab6cf32e89fbc697453b14fa04", 2611),
    "src/cu_pool.c": ("d495eb65feb6e6ee15f8bbad0b9b38b155ca0ca792d9ba7643f1160020eb0ce0", 5306),
    "src/cu_process.c": ("09beebe9d14e446163a82a0f5d753f5e5ad3559111af430a59f46a50ba104288", 9985),
    "src/cu_process_io.c": ("ff61fb4bb01260cf81ed3fd69f99025c6e85154b437c3d9c185841b6702a09df", 10421),
    "src/cu_reader.c": ("0e5ea1d30ba6db33b9a0e3e9f57f3cc3f868dbba6496b1b42a0e62171a84350a", 4204),
    "src/cu_sha256.c": ("80a1723a30bd2cc4cd38a88a03d05f84da8acc66e61948875cf349b6137a1fb9", 5195),
    "src/cu_socket.c": ("47aaf164c99e3d2a4ef178b353f069cb55dcb7f07408f8f87c0908b27b64216b", 24782),
    "src/cu_str.c": ("bf95df848f00588b4782f00db92fae3c358a64bbe2abbcd682f6e54d53ae0111", 7712),
    "src/cu_task.c": ("c6bfe0d7909a663fe7607d37e31ed9c32bdda24419ecd265f6716398d0d79010", 5866),
    "src/cu_thread.c": ("f4f81136fa973785a21a3e741998ef5e14de9aa2b289794c08c72481b7e5a483", 5481),
    "src/cu_u64.c": ("ea990339276957591d2c04823c4350c489159500ec4a0d8abadffbb10cab3878", 2493),
    "src/cu_vec.c": ("86b7b1201e74f035959b4984b7fa44ca1bc0b67a289cbec8c3cd1920163bcd91", 5428),
}
# The compiled subset; the rest of the inventory is pinned but never compiled.
COMPILED = [
    "src/cu_append.c",
    "src/cu_arena.c",
    "src/cu_bytes.c",
    "src/cu_dir.c",
    "src/cu_file.c",
    "src/cu_fmt.c",
    "src/cu_input.c",
    "src/cu_map.c",
    "src/cu_metadata.c",
    "src/cu_plex.c",
    "src/cu_pool.c",
    "src/cu_sha256.c",
    "src/cu_str.c",
    "src/cu_thread.c",
    "src/cu_u64.c",
    "src/cu_vec.c",
]
EXPECTED_MANIFEST = {
    "schema": "fynd.cutils-pin/2",
    "revision": REVISION,
    "files": {name: {"sha256": digest, "size": size}
              for name, (digest, size) in PINNED.items()},
    "sources": COMPILED,
}
STRICT = "-std=c89 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes".split()
DEFAULT_PROJECT = Path(__file__).resolve().parents[1]
OPTIONS = argparse.Namespace(project=DEFAULT_PROJECT,
                             cutils=DEFAULT_PROJECT.parent / "c-utils",
                             build_root=None)

# This is a boundary sensor, not a replacement finder or a behavioral mutant.
# Each invocation gets its own record, so parallel recipes cannot clobber it.
FAKE_CC = r'''import hashlib
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile

config = json.loads(Path(__file__).with_suffix('.json').read_text())
args = sys.argv[1:]
fd, name = tempfile.mkstemp(dir=config['records'], suffix='.json')
os.close(fd)
record = Path(name)
result = {'argv': args}
record.write_text(json.dumps(result))  # marker precedes any inspection/failure
output = Path(args[args.index('-o') + 1])
result['output'] = str(output.resolve())
if config.get('mutate'):
    with open(config['mutate'], 'ab') as stream:
        stream.write(b'\n/* original changed after compiler entry */\n')
if config.get('inspect'):
    sources = [Path(arg).resolve() for arg in args
               if arg.endswith('.c') and Path(arg).name.startswith('cu_')]
    includes = []
    for index, arg in enumerate(args):
        if arg == '-I':
            includes.append(Path(args[index + 1]).resolve())
        elif arg.startswith('-I'):
            includes.append(Path(arg[2:]).resolve())
    headers = [path / 'cu.h' for path in includes if (path / 'cu.h').is_file()]
    result['sources'] = [str(path) for path in sources]
    result['headers'] = [str(path) for path in headers]
    result['inputs'] = {
        str(path): {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                    'size': path.stat().st_size}
        for path in sources + headers
    }
record.write_text(json.dumps(result))
output.parent.mkdir(parents=True, exist_ok=True)
if config.get('fail'):
    output.write_bytes(b'partial compiler output\n')
    raise SystemExit(19)
output.write_text('#!/bin/sh\nprintf "%s\\n" ran >> ' +
                  shlex.quote(config['executions']) + '\nexit 0\n')
output.chmod(0o755)
'''


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class BuildTests(unittest.TestCase):
    def setUp(self):
        if OPTIONS.build_root is not None:
            OPTIONS.build_root.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="fynd-build-",
                                                dir=OPTIONS.build_root)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.project = self.root / "fynd"
        self.project.mkdir()
        for name in ("src", "include", "cmd"):
            shutil.copytree(OPTIONS.project / name, self.project / name)
        (self.project / "tests").mkdir()
        for source in (OPTIONS.project / "tests").glob("test_*.c"):
            shutil.copy2(source, self.project / "tests" / source.name)
        for name in ("Makefile", "CUTILS_REVISION"):
            shutil.copy2(OPTIONS.project / name, self.project / name)
        if (OPTIONS.project / "ci").is_dir():
            shutil.copytree(OPTIONS.project / "ci", self.project / "ci",
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        self.cutils = self.root / "c-utils"
        self.restore_dependency()
        # Fail clearly on an unsuitable fixture, without needing candidate ci/.
        actual = {str(path.relative_to(self.cutils)): (fingerprint(path), path.stat().st_size)
                  for folder in ("include", "src")
                  for path in (self.cutils / folder).iterdir() if path.is_file()}
        self.assertEqual(actual, PINNED, "--cutils must supply the exact pinned include/src export")
        self.build_name = "build"
        self.records = self.root / "records"
        self.records.mkdir()
        self.executions = self.root / "executions"
        self.scratch = self.root / "scratch"
        self.scratch.mkdir()
        self.fake = self.root / "fakecc.py"
        self.fake.write_text(FAKE_CC)
        self.config = {"records": str(self.records), "executions": str(self.executions)}
        self.configure_compiler()
        self.cc = shlex.join([sys.executable, str(self.fake)])

    @property
    def output(self):
        return self.project / self.build_name

    @property
    def test_names(self):
        return sorted(path.stem for path in (self.project / "tests").glob("test_*.c"))

    def restore_dependency(self):
        if self.cutils.exists():
            shutil.rmtree(self.cutils)
        for name in ("include", "src"):
            shutil.copytree(OPTIONS.cutils / name, self.cutils / name, symlinks=True)

    def configure_compiler(self, **changes):
        self.config.update(changes)
        self.fake.with_suffix(".json").write_text(json.dumps(self.config))

    def run_process(self, argv, timeout=90):
        env = os.environ.copy()
        env.pop("MAKEFLAGS", None)
        env.pop("MFLAGS", None)
        env.pop("MAKEOVERRIDES", None)
        env["TMPDIR"] = str(self.scratch)
        process = subprocess.Popen([str(arg) for arg in argv], cwd=self.project,
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            stdout, stderr = process.communicate()
            self.fail("timed out (child process group killed and reaped): {}\n{}\n{}".format(
                shlex.join([str(arg) for arg in argv]), stdout, stderr))
        return subprocess.CompletedProcess(argv, process.returncode, stdout, stderr)

    def make(self, *args, timeout=90):
        return self.run_process(["make", "--no-print-directory", "CUTILS=" + str(self.cutils),
                                 "BUILD=" + self.build_name, "PYTHON=" + sys.executable,
                                 "CC=" + self.cc, *args], timeout=timeout)

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def compiler_records(self):
        return [json.loads(path.read_text()) for path in sorted(self.records.glob("*.json"))]

    def clear_markers(self):
        for path in self.records.iterdir():
            path.unlink()
        self.executions.unlink(missing_ok=True)

    def seed_outputs(self, names=("fynd",)):
        self.output.mkdir(exist_ok=True)
        for name in names:
            path = self.output / name
            path.write_bytes(b"#!/bin/sh\nexit 87\n# preserved old binary\n")
            path.chmod(0o755)
            os.utime(path, (1, 1))
        return self.output_hashes()

    def output_hashes(self):
        return {path.name: fingerprint(path) for path in self.output.iterdir() if path.is_file()}

    def layout(self):
        return {path.relative_to(self.root) for path in self.root.rglob("*")
                if self.records not in path.parents and path != self.executions}

    def assert_no_temporaries(self, before):
        allowed = {self.output.relative_to(self.root)}
        allowed.update((self.output / name).relative_to(self.root)
                       for name in ["fynd", *self.test_names])
        self.assertFalse(self.layout() - before - allowed,
                         "private snapshots/output files leaked after recipe completion")
        self.assertEqual(list(self.scratch.iterdir()), [], "temporary snapshot leaked in TMPDIR")

    def assert_guard(self, result, old_outputs=None):
        self.assertNotEqual(result.returncode, 0,
                            "expected pin rejection before compiler; make accepted unpinned dependency\n" +
                            result.stdout + result.stderr)
        self.assertTrue(any(line.startswith("c-utils pin:") for line in result.stderr.splitlines()),
                        "expected c-utils pin: diagnostic, not a Make/compile failure\n" + result.stderr)
        self.assertEqual(self.compiler_records(), [], "compiler ran before dependency refusal")
        self.assertFalse(self.executions.exists(), "test executable ran before dependency refusal")
        if old_outputs is not None:
            self.assertEqual(self.output_hashes(), old_outputs, "guard failure changed old binaries")

    def mutate(self, kind):
        source = self.cutils / "src/cu_vec.c"
        header = self.cutils / "include/cu.h"
        if kind in ("source", "header"):
            path = source if kind == "source" else header
            info = path.stat()
            with path.open("ab") as stream:
                stream.write(b"\n/* deliberately not the pinned bytes */\n")
            # Timestamp-only validation and already-current targets must not bypass the guard.
            os.utime(path, ns=(info.st_atime_ns, info.st_mtime_ns))
        elif kind in ("missing-source", "missing-header"):
            (source if kind == "missing-source" else header).unlink()
        elif kind == "extra-source":
            (self.cutils / "src/cu_unpinned.c").write_text("/* extra translation unit */\n")
        elif kind == "extra-header":
            (self.cutils / "include/unpinned.h").write_text("/* unexpected header */\n")
        elif kind in ("symlink-source", "symlink-header"):
            path = source if kind == "symlink-source" else header
            backing = self.root / (path.name + ".backing")
            backing.write_bytes(path.read_bytes())
            path.unlink()
            path.symlink_to(backing)
        elif kind == "fifo-source":
            source.unlink()
            os.mkfifo(source)
        elif kind == "fifo-header":
            header.unlink()
            os.mkfifo(header)
        else:
            raise AssertionError(kind)

    def targets(self):
        return [("all",), (self.build_name + "/fynd",), ("test",),
                *( (self.build_name + "/" + name,) for name in self.test_names),
                ("-j2", "all", "test")]

    def test_modified_source_refused(self):
        """Selected baseline RED: old real Make accepts a modified cu_vec.c."""
        old_outputs = self.seed_outputs()
        self.mutate("source")
        before = self.layout()
        self.assert_guard(self.make("all"), old_outputs)
        self.assert_no_temporaries(before)

    def test_manifest_exact_fixed_pin(self):
        manifest = json.loads((self.project / "ci/pinned_cutils.json").read_text())
        self.assertEqual(manifest, EXPECTED_MANIFEST)
        self.assertEqual((self.project / "CUTILS_REVISION").read_text().strip(), REVISION)
        self.assertEqual(self.test_names,
                         ["test_cursor", "test_fynd", "test_ignore", "test_sched", "test_stream"],
                         "all five native C suites must be copied/discovered")
        self.assertFalse((self.cutils / ".git").exists())

    def test_invalid_inventory_refused(self):
        for kind in ("header", "missing-source", "missing-header", "extra-source", "extra-header",
                     "symlink-source", "symlink-header", "fifo-source", "fifo-header"):
            with self.subTest(kind=kind):
                self.restore_dependency()
                self.clear_markers()
                old_outputs = self.seed_outputs()
                self.mutate(kind)
                before = self.layout()
                self.assert_guard(self.make("all", timeout=10), old_outputs)
                self.assert_no_temporaries(before)

    def test_wrong_project_revision_refused(self):
        old_outputs = self.seed_outputs()
        (self.project / "CUTILS_REVISION").write_text("0" * 40 + "\n")
        self.assert_guard(self.make("all"), old_outputs)

    def test_invalid_manifest_refused(self):
        path = self.project / "ci/pinned_cutils.json"
        self.assertTrue(path.is_file(), "candidate must provide the fixed manifest")
        for kind in ("schema", "revision", "missing", "extra", "digest", "size",
                     "unpinned-source", "dropped-source", "unsorted-sources"):
            with self.subTest(kind=kind):
                manifest = json.loads(json.dumps(EXPECTED_MANIFEST))
                if kind in ("schema", "revision"):
                    manifest[kind] = "unsupported"
                elif kind == "missing":
                    del manifest["files"]["src/cu_vec.c"]
                elif kind == "extra":
                    manifest["files"]["src/extra.c"] = {"sha256": "0" * 64, "size": 0}
                elif kind == "digest":
                    manifest["files"]["src/cu_vec.c"]["sha256"] = "0" * 64
                elif kind == "unpinned-source":
                    manifest["sources"][-1] = "src/cu_unpinned.c"
                elif kind == "dropped-source":
                    manifest["sources"].remove("src/cu_metadata.c")
                elif kind == "unsorted-sources":
                    manifest["sources"].reverse()
                else:
                    manifest["files"]["src/cu_vec.c"]["size"] += 1
                path.write_text(json.dumps(manifest))
                self.clear_markers()
                old_outputs = self.seed_outputs()
                self.assert_guard(self.make("all"), old_outputs)

    def test_fresh_targets_guard_before_compiler(self):
        self.mutate("source")
        for targets in self.targets():
            with self.subTest(targets=targets):
                self.clear_markers()
                self.assert_guard(self.make(*targets))
                self.assertFalse(self.output.exists() and any(self.output.iterdir()),
                                 "fresh invalid build published output")

    def test_current_targets_guard_and_missing_inputs_do_not_fall_through(self):
        self.assert_success(self.make("-j2", "all", "test"))
        self.assertEqual(len(self.compiler_records()), 1 + len(self.test_names))
        old_outputs = self.output_hashes()
        self.assertEqual(set(old_outputs), {"fynd", *self.test_names})
        for kind in ("source", "missing-source", "missing-header"):
            for targets in self.targets():
                with self.subTest(kind=kind, targets=targets):
                    self.restore_dependency()
                    self.mutate(kind)
                    self.clear_markers()
                    self.assert_guard(self.make(*targets), old_outputs)

    def test_missing_policy_cannot_hide_direct_test_rule(self):
        self.assert_success(self.make("-j2", "all", "test"))
        self.clear_markers()
        old_outputs = self.output_hashes()
        for name in ("ci/pinned_cutils.py", "ci/pinned_cutils.json", "CUTILS_REVISION"):
            with self.subTest(policy=name):
                path = self.project / name
                saved = path.read_bytes()
                path.unlink()
                try:
                    result = self.make(str(Path(self.build_name) / self.test_names[0]))
                    self.assertNotEqual(result.returncode, 0,
                                        "missing policy hid the direct test rule; Make accepted old output")
                    self.assertEqual(self.compiler_records(), [])
                    self.assertEqual(self.output_hashes(), old_outputs)
                    self.assertFalse(self.executions.exists())
                finally:
                    path.write_bytes(saved)

    def test_valid_current_targets_do_not_recompile(self):
        before = self.layout()
        self.assert_success(self.make("-j2", "all", "test"))
        old_outputs = self.output_hashes()
        old_times = {name: (self.output / name).stat().st_mtime_ns for name in old_outputs}
        for targets in self.targets():
            with self.subTest(targets=targets):
                self.clear_markers()
                self.assert_success(self.make(*targets))
                self.assertEqual(self.compiler_records(), [], "guard must be order-only, not force relinking")
                self.assertEqual(self.output_hashes(), old_outputs)
                self.assertEqual({name: (self.output / name).stat().st_mtime_ns for name in old_outputs},
                                 old_times)
        self.assert_no_temporaries(before)

    def assert_private_inputs(self, record):
        sources = [Path(name) for name in record["sources"]]
        headers = [Path(name) for name in record["headers"]]
        self.assertEqual(len(sources), 16)
        self.assertEqual(len(headers), 1)
        self.assertEqual(["src/" + path.name for path in sources], COMPILED)
        snapshot = headers[0].parent.parent
        self.assertEqual(headers[0], snapshot / "include/cu.h")
        self.assertEqual({path.parent for path in sources}, {snapshot / "src"})
        self.assertNotEqual(snapshot, self.cutils, "compiler consumed mutable dependency paths")
        self.assertFalse(snapshot == self.cutils or self.cutils in snapshot.parents)
        observed = {str(Path(name).relative_to(snapshot)): value
                    for name, value in record["inputs"].items()}
        expected = {name: EXPECTED_MANIFEST["files"][name] for name in ["include/cu.h", *COMPILED]}
        self.assertEqual(observed, expected, "compiler inputs were not the pinned snapshot")
        self.assertFalse(snapshot.exists(), "private dependency snapshot was not removed")
        private_output = Path(record["output"])
        self.assertNotIn(private_output, [self.output / name for name in ["fynd", *self.test_names]])
        self.assertFalse(private_output.exists(), "private compiler output leaked")
        for index, arg in enumerate(record["argv"]):
            value = (record["argv"][index + 1] if arg == "-I" else
                     arg[2:] if arg.startswith("-I") else arg)
            path = Path(value)
            path = (self.project / path).resolve()
            self.assertFalse(path == self.cutils or self.cutils in path.parents,
                             "original dependency path reached compiler argv")
        return snapshot

    def test_snapshot_stable_after_original_mutates_and_flags_are_preserved(self):
        self.build_name = "native-custom"
        old_outputs = self.seed_outputs()
        self.configure_compiler(inspect=True, mutate=str(self.cutils / "src/cu_vec.c"))
        before = self.layout()
        result = self.make("CPPFLAGS=-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DFYND_CPP_FLAG=1",
                           "CFLAGS=-O0 -g -DFYND_BUILD_FLAG=1", "all")
        self.assert_success(result)
        records = self.compiler_records()
        self.assertEqual(len(records), 1)
        self.assert_private_inputs(records[0])
        for flag in [*STRICT, "-O0", "-g", "-DFYND_BUILD_FLAG=1", "-DFYND_CPP_FLAG=1",
                     "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE"]:
            self.assertIn(flag, records[0]["argv"])
        app_sources = {(self.project / arg).resolve() for arg in records[0]["argv"]
                       if arg.endswith(".c")}
        self.assertIn(self.project / "cmd/main.c", app_sources)
        self.assertIn(self.project / "src/fynd.c", app_sources)
        self.assertNotEqual(fingerprint(self.cutils / "src/cu_vec.c"), PINNED["src/cu_vec.c"][0])
        self.assertNotEqual(self.output_hashes(), old_outputs, "successful build failed to replace old output")
        self.assertTrue(os.access(self.output / "fynd", os.X_OK))
        self.assert_no_temporaries(before)
        published = self.output_hashes()
        self.clear_markers()
        self.assert_guard(self.make("all"), published)

    def test_parallel_recipes_have_independent_private_snapshots(self):
        self.configure_compiler(inspect=True)
        before = self.layout()
        self.assert_success(self.make("-j2", "all", "test"))
        records = self.compiler_records()
        self.assertEqual(len(records), 1 + len(self.test_names))
        snapshots = [self.assert_private_inputs(record) for record in records]
        self.assertEqual(len(set(snapshots)), len(records), "parallel recipes shared a snapshot")
        self.assertEqual(len({record["output"] for record in records}), len(records))
        self.assert_no_temporaries(before)

    def test_compiler_failure_preserves_old_binary_and_cleans_private_files(self):
        old_outputs = self.seed_outputs()
        self.configure_compiler(fail=True)
        before = self.layout()
        result = self.make("all")
        self.assertNotEqual(result.returncode, 0, "compiler failure was ignored")
        self.assertEqual(len(self.compiler_records()), 1, "valid dependency never reached compiler")
        self.assertEqual(self.output_hashes(), old_outputs, "partial compiler output replaced old binary")
        self.assert_no_temporaries(before)

    def test_missing_compiler_preserves_old_binary(self):
        old_outputs = self.seed_outputs()
        before = self.layout()
        result = self.make("CC=" + str(self.root / "compiler-does-not-exist"), "all")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output_hashes(), old_outputs)
        self.assertEqual(self.compiler_records(), [])
        self.assert_no_temporaries(before)

    def test_helper_check_api(self):
        helper = self.project / "ci/pinned_cutils.py"
        self.assertTrue(helper.is_file(), "candidate must provide the stdlib helper")
        command = [sys.executable, "-I", helper, "check", "--cutils", self.cutils]
        self.assert_success(self.run_process(command))
        self.mutate("header")
        self.assert_guard(self.run_process(command))

    def test_helper_build_api(self):
        helper = self.project / "ci/pinned_cutils.py"
        self.assertTrue(helper.is_file(), "candidate must provide the stdlib helper")
        old_outputs = self.seed_outputs()
        self.configure_compiler(inspect=True)
        before = self.layout()
        command = [sys.executable, "-I", helper, "build", "--cutils", self.cutils,
                   "--output", self.output / "fynd", "--", sys.executable, self.fake,
                   *STRICT, "-Iinclude", "cmd/main.c", "src/fynd.c", "-pthread"]
        self.assert_success(self.run_process(command))
        records = self.compiler_records()
        self.assertEqual(len(records), 1)
        self.assert_private_inputs(records[0])
        self.assertNotEqual(self.output_hashes(), old_outputs)
        self.assert_no_temporaries(before)
        published = self.output_hashes()
        self.clear_markers()
        self.mutate("header")
        self.assert_guard(self.run_process(command), published)

    def test_clean_needs_neither_dependency_nor_python_checker(self):
        self.seed_outputs(["fynd", *self.test_names])
        shutil.rmtree(self.cutils)
        self.assert_success(self.make("PYTHON=" + str(self.root / "missing-python"), "clean"))
        self.assertEqual(self.output_hashes(), {})
        self.assertEqual(self.compiler_records(), [])

    def test_real_strict_c89_pinned_archive_build(self):
        self.build_name = "real-native"
        before = self.layout()
        # Unlike the sentinel tests, this invokes the system C compiler.
        self.assert_success(self.make("CC=cc", "CFLAGS=-O0 -g", "all", timeout=120))
        binary = self.output / "fynd"
        self.assertTrue(binary.is_file())
        version = self.run_process([binary, "--version"])
        self.assert_success(version)
        self.assertEqual(version.stdout.strip(), "fynd 0.1.0")
        self.assertEqual(version.stderr, "")
        self.assert_no_temporaries(before)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--cutils", type=Path)
    parser.add_argument("--build-root", type=Path)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.project = OPTIONS.project.expanduser().resolve()
    OPTIONS.cutils = (OPTIONS.cutils.expanduser().resolve() if OPTIONS.cutils is not None
                      else OPTIONS.project.parent / "c-utils")
    if OPTIONS.build_root is not None:
        OPTIONS.build_root = OPTIONS.build_root.expanduser().resolve()
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
