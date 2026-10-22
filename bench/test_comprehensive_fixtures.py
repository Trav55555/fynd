"""Fixture tests only; no production binary, full-size build, or source copy.

Run with stdlib unittest.  Git subprocesses, when available, are confined to a
TemporaryDirectory repository made by the test, never the working repository.
"""

from __future__ import annotations

import fnmatch
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest
from unittest import mock

if __package__:
    from . import comprehensive_fixtures as fixtures
else:
    import comprehensive_fixtures as fixtures


def filesystem_nodes(root: bytes) -> dict[bytes, str]:
    """Independent iterative filesystem enumeration; never recurse into links."""
    result = {b"": "d"}
    pending = [(root, b"")]
    while pending:
        absolute, relative = pending.pop()
        with os.scandir(absolute) as entries:
            for entry in entries:
                path = relative + b"/" + entry.name if relative else entry.name
                mode = entry.stat(follow_symlinks=False).st_mode
                if stat.S_ISDIR(mode):
                    result[path] = "d"
                    pending.append((entry.path, path))
                elif stat.S_ISREG(mode):
                    result[path] = "f"
                elif stat.S_ISLNK(mode):
                    result[path] = "l"
                else:
                    if not stat.S_ISFIFO(mode):
                        raise AssertionError("unexpected fixture node type")
                    result[path] = "o"
    return result


def hidden(path: bytes) -> bool:
    # The explicit root is not subject to hidden pruning.
    return bool(path) and any(part.startswith(b".") for part in path.split(b"/"))


def spec_excluded(path: bytes, kind: str) -> bool:
    """Test-side policy, deliberately independent of fixture implementation."""
    directories = path.split(b"/") if kind == "d" else path.split(b"/")[:-1]
    for directory in directories:
        if directory in (b".git", b"git-drop", b"plain-drop", b"excluded",
                         b"global-drop", b"local-drop"):
            return True
    basename = path.rsplit(b"/", 1)[-1]
    return kind != "d" and basename.endswith(b".tmp") and basename != b"keep-me.tmp"


class DatasetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)

    def test_fields_parents_empty_directories_and_exact_types(self):
        provenance = {"test": "owned"}
        dataset = fixtures.Dataset("sample", self.base, "test", provenance=provenance)
        self.assertEqual(dataset.label, "sample")
        self.assertEqual(dataset.base, self.base)
        self.assertEqual(dataset.root, os.fsencode(self.base / "sample"))
        self.assertEqual(dataset.family, "test")
        self.assertEqual(dataset.policy, "none")
        self.assertEqual(dataset.provenance, provenance)
        self.assertIsNot(dataset.provenance, provenance)
        dataset.directory()
        dataset.directory(b"empty/inner")
        dataset.file(b"parents/child/file", b"generated")
        dataset.link(b"links/file", b"missing")
        dataset.fifo(b"pipes/fifo")
        expected = {b"": "d", b"empty": "d", b"empty/inner": "d",
                    b"parents": "d", b"parents/child": "d",
                    b"parents/child/file": "f", b"links": "d",
                    b"links/file": "l", b"pipes": "d", b"pipes/fifo": "o"}
        self.assertEqual(dataset.nodes, expected)
        self.assertEqual(filesystem_nodes(dataset.root), expected)
        self.assertEqual(os.readlink(dataset.root + b"/links/file"), b"missing")
        with open(dataset.root + b"/parents/child/file", "rb") as stream:
            self.assertEqual(stream.read(), b"generated")
        dataset.directory(b"empty/inner")  # Idempotent directories only.
        for action in (lambda: dataset.file(b"parents/child/file"),
                       lambda: dataset.link(b"links/file", b"other"),
                       lambda: dataset.fifo(b"pipes/fifo")):
            with self.assertRaises(FileExistsError):
                action()

    def test_rejects_escaping_or_ambiguous_paths_before_creation(self):
        dataset = fixtures.Dataset("safe", self.base, "test")
        invalid = (b"/absolute", b"../escape", b"a/../../escape", b"a/../b",
                   b"a//b", b"./file", b"a/./b", b"a/", b"null\0byte")
        for path in invalid:
            for method in (dataset.directory, dataset.file, dataset.fifo,
                           lambda relative: dataset.link(relative, b"safe-target")):
                with self.subTest(path=path, method=method):
                    with self.assertRaises(ValueError):
                        method(path)
        for method in (dataset.directory, dataset.file, dataset.fifo):
            with self.assertRaises(TypeError):
                method("not-bytes")
        for method in (dataset.file, dataset.fifo,
                       lambda path: dataset.link(path, b"target")):
            with self.assertRaises(ValueError):
                method(b"")
        for target in (b"/absolute", b"../escape", b"a/../b", b""):
            with self.assertRaises(ValueError):
                dataset.link(b"bad-target", target)
        with self.assertRaises(TypeError):
            dataset.link(b"bad-target", "text-target")
        with self.assertRaises(TypeError):
            dataset.file(b"bad-content", "text-content")
        self.assertEqual(dataset.nodes, {b"": "d"})
        self.assertEqual(filesystem_nodes(dataset.root), dataset.nodes)

    def test_labels_and_existing_roots_cannot_overwrite_sources(self):
        for label in ("", ".", "..", "/absolute", "../escape", "two/components", "a\0b"):
            with self.subTest(label=label), self.assertRaises(ValueError):
                fixtures.Dataset(label, self.base, "test")
        with self.assertRaises(TypeError):
            fixtures.Dataset(b"bytes-label", self.base, "test")
        with self.assertRaises(ValueError):
            fixtures.Dataset("bad-policy", self.base, "test", policy="git")
        source = self.base / "source"
        source.mkdir()
        sentinel = source / "sentinel"
        sentinel.write_bytes(b"preserve")
        with self.assertRaises(FileExistsError):
            fixtures.Dataset("source", self.base, "test")
        self.assertEqual(sentinel.read_bytes(), b"preserve")

    def test_symlink_parents_are_never_followed_even_if_replaced(self):
        dataset = fixtures.Dataset("safe", self.base, "test")
        dataset.directory(b"actual")
        dataset.link(b"alias", b"actual")
        with self.assertRaises(ValueError):
            dataset.file(b"alias/not-created")
        outside = self.base / "outside"
        outside.mkdir()
        dataset.directory(b"replaced")
        os.rmdir(dataset.root + b"/replaced")
        os.symlink(os.fsencode(outside), dataset.root + b"/replaced")
        for method in (dataset.directory, dataset.file, dataset.fifo):
            with self.assertRaises(OSError):
                method(b"replaced/not-created")
        self.assertEqual(list(outside.iterdir()), [])
        self.assertFalse((self.base / "safe" / "actual" / "not-created").exists())

    def test_raw_byte_names_and_nonfollowed_links(self):
        dataset = fixtures.Dataset("raw", self.base, "raw")
        dataset.file(b"\xff\n/\xfe.c")
        dataset.directory(b"empty")
        dataset.link(b"directory-link", b"empty")
        dataset.link(b"cycle", b"cycle")
        dataset.fifo(b"\xff-pipe")
        self.assertEqual(filesystem_nodes(dataset.root), dataset.nodes)
        self.assertEqual(dataset.nodes[b"directory-link"], "l")
        self.assertNotIn(b"directory-link/anything", dataset.nodes)


class QuickBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        try:
            cls.base = Path(cls.temporary.name)
            cls.first = fixtures.build(cls.base / "first", True, [], [], cls.base / "ram-one")
            cls.second = fixtures.build(cls.base / "second", True, [], [], cls.base / "ram-two")
        except BaseException:
            cls.temporary.cleanup()
            raise
        cls.by_label = {dataset.label: dataset for dataset in cls.first}

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_labels_families_actual_parameters_and_determinism(self):
        expected = [("empty", "empty"), ("tiny", "tiny"), ("flat10k", "flat"),
                    ("flat100k", "flat"), ("flat1m", "flat"), ("deep", "deep"),
                    ("balanced", "balanced"), ("manydirs", "manydirs"),
                    ("prefix", "prefix"), ("skew", "skew"), ("output", "output"),
                    ("sharedprefix", "sharedprefix"), ("raw", "raw"),
                    ("ignore-prune", "ignore"), ("ignore64", "ignore"),
                    ("ignore512", "ignore"), ("ram-flat", "ram"), ("ram-manydirs", "ram")]
        self.assertEqual([(d.label, d.family) for d in self.first], expected)
        self.assertEqual(self.by_label["empty"].nodes, {b"": "d"})
        for one, two in zip(self.first, self.second):
            with self.subTest(label=one.label):
                self.assertEqual(one.nodes, two.nodes)
                self.assertEqual(one.provenance, two.provenance)
                self.assertEqual(one.policy, "ignore" if one.family == "ignore" else "none")
                self.assertIsInstance(one.root, bytes)
                self.assertTrue(one.provenance["quick"])
                self.assertEqual(one.provenance["actual_nodes"], len(one.nodes))
                for kind in ("d", "f", "l", "o"):
                    self.assertEqual(one.provenance["actual_counts"][kind],
                                     list(one.nodes.values()).count(kind))
                json.dumps(one.provenance)
        for label in ("flat10k", "flat100k", "flat1m", "sharedprefix", "ram-flat"):
            dataset = self.by_label[label]
            self.assertEqual(dataset.provenance["parameters"]["files"],
                             dataset.provenance["actual_counts"]["f"])
        # Quick tests intentionally do NOT expect the million-file full shape.
        self.assertLess(sum(len(d.nodes) for d in self.first), 10_000)
        self.assertEqual(self.by_label["ram-flat"].base, self.base / "ram-one")
        self.assertEqual(self.by_label["ram-manydirs"].base, self.base / "ram-one")
        self.assertEqual(self.by_label["flat10k"].base, self.base / "first")

    def test_every_manifest_agrees_with_actual_filesystem(self):
        for dataset in self.first + self.second:
            with self.subTest(root=dataset.root):
                self.assertEqual(filesystem_nodes(dataset.root), dataset.nodes)
                for path, kind in dataset.nodes.items():
                    if kind == "f" and path.rsplit(b"/", 1)[-1] not in (
                            b".ignore", b".gitignore", b"exclude"):
                        self.assertEqual(os.lstat(dataset.root + b"/" + path).st_size, 0)

    def test_case_extensions_long_names_prefix_collision_and_all_raw_features(self):
        raw = self.by_label["raw"]
        for name in (b"a", b"a/z", b"a-", b"z", b"ASCII.c", b"ascii.c", b"ASCII.C",
                     b"ascii.txt", b"line\nbreak.c", b"high-\xff.txt", b".hidden.c",
                     b".hidden-dir/visible.c", b"named-pipe"):
            self.assertIn(name, raw.nodes)
        self.assertEqual(raw.nodes[b"a"], "d")
        self.assertEqual(raw.nodes[b"a-"], "f")
        self.assertLess(b"a-", b"a/z")  # Raw full-path order differs from DFS order.
        self.assertEqual(sum(raw.nodes.get(b"dir-%02d" % i) == "d" for i in range(40)), 40)
        self.assertEqual(raw.nodes[b"named-pipe"], "o")
        for link in (b"file-link", b"dir-link", b"dangling-link", b"loop-link"):
            self.assertEqual(raw.nodes[link], "l")
        flat = self.by_label["flat1m"]
        basenames = list(path for path, kind in flat.nodes.items() if kind == "f")
        for index in range(len(basenames)):
            name = basenames[index]
            self.assertEqual(name.startswith(b"."), index % 7 == 0)
            self.assertEqual(b"needle" in name.lower(), index % 11 == 0)
            self.assertIn(b"%07d" % index, name)
        for suffix in (b".c", b".C", b".txt"):
            self.assertTrue(any(name.endswith(suffix) for name in basenames))
            self.assertEqual(flat.provenance["actual_extension_counts"][suffix.decode()],
                             sum(name.endswith(suffix) for name in basenames))
        shared = self.by_label["sharedprefix"]
        self.assertTrue(all(path.lstrip(b".").startswith(b"s" * 200)
                            for path, kind in shared.nodes.items() if kind == "f"))
        output = self.by_label["output"]
        self.assertEqual(sum(kind == "d" for kind in output.nodes.values()), 9)
        self.assertTrue(all(len(path.rsplit(b"/", 1)[-1]) > 200
                            for path, kind in output.nodes.items() if kind == "f"))

    def test_hidden_and_ignore_are_independent_policies(self):
        for label in ("raw", "ignore-prune", "ignore64", "ignore512"):
            dataset = self.by_label[label]
            actual = filesystem_nodes(dataset.root)
            for show_hidden in (False, True):
                expected = {path for path, kind in dataset.nodes.items()
                            if (show_hidden or not hidden(path)) and
                            (dataset.policy == "none" or not spec_excluded(path, kind))}
                selected = {path for path, kind in actual.items()
                            if (show_hidden or not hidden(path)) and
                            not fixtures.ignored(dataset, path, kind)}
                self.assertEqual(selected, expected)
            # Hidden-disabled results exclude dot directories and their visible
            # descendants, but never the explicitly supplied dataset root.
            self.assertFalse(hidden(b""))
            self.assertTrue(hidden(b".parent/visible.c"))
            self.assertIn(b"", actual)
        ignore = self.by_label["ignore-prune"]
        self.assertFalse(fixtures.ignored(ignore, b".gitignore", "f"))
        self.assertFalse(fixtures.ignored(ignore, b".ignore", "f"))
        self.assertTrue(hidden(b".gitignore"))
        self.assertTrue(hidden(b".ignore"))

    def test_ignore_rules_and_kept_dropped_sources_present_in_every_ignore_root(self):
        for label, count in (("ignore-prune", 0), ("ignore64", 64), ("ignore512", 512)):
            dataset = self.by_label[label]
            expected_rules = {
                b".git/info/exclude": b"excluded/\n",
                b".gitignore": b"git-drop/\n*.tmp\n!keep-me.tmp\n",
                b"keep/d00/.gitignore": b"local-drop/\n",
            }
            for path, content in expected_rules.items():
                with open(dataset.root + b"/" + path, "rb") as stream:
                    self.assertEqual(stream.read(), content)
            with open(dataset.root + b"/.ignore", "rb") as stream:
                rules = stream.read().splitlines()
            self.assertEqual(rules[:2], [b".git/", b"plain-drop/"])
            self.assertEqual(len(rules[2:]), count)
            for pattern in rules[2:]:
                self.assertFalse(any(fnmatch.fnmatchcase(path.rsplit(b"/", 1)[-1], pattern)
                                     for path in dataset.nodes))
            for directory in (b".git", b"git-drop", b"plain-drop", b"excluded", b"global-drop",
                              b"keep/git-drop", b"keep/plain-drop", b"keep/excluded",
                              b"keep/global-drop", b"keep/d00/local-drop"):
                self.assertEqual(dataset.nodes[directory], "d")
                self.assertTrue(fixtures.ignored(dataset, directory, "d"))
            for parent in (b"", b"keep/", b"keep/d00/", b"keep/d00/kept/"):
                self.assertEqual(dataset.nodes[parent + b"drop.tmp"], "f")
                self.assertTrue(fixtures.ignored(dataset, parent + b"drop.tmp", "f"))
                self.assertEqual(dataset.nodes[parent + b"keep-me.tmp"], "f")
                self.assertFalse(fixtures.ignored(dataset, parent + b"keep-me.tmp", "f"))
            self.assertEqual(dataset.provenance["parameters"]["miss_only_patterns"], count)

    def test_duplicate_labels_rejected_before_any_destinations_are_created(self):
        for git_trees, layouts in (([("raw", self.base, "HEAD")], []),
                                  ([], [("new", self.base), ("new", self.base)])):
            destination = self.base / "must-not-create"
            with self.assertRaises(ValueError):
                fixtures.build(destination, True, git_trees, layouts, None)
            self.assertFalse(destination.exists())


class IgnoreOracleTests(unittest.TestCase):
    def test_explicit_cases_without_reading_ignore_rules(self):
        with tempfile.TemporaryDirectory() as temporary:
            dataset = fixtures.Dataset(".explicit-root", Path(temporary), "ignore", "ignore")
            # There are deliberately no rule files: this is a fixed SPEC oracle.
            cases = [
                (b"", "d", False), (b".hidden", "f", False),
                (b".hidden-dir/visible.c", "f", False),
                (b".git", "d", True), (b".git", "f", False),
                (b".git/info/exclude", "f", True),
                (b"plain-drop", "f", False), (b"plain-drop", "d", True),
                (b"x/plain-drop/y/keep-me.tmp", "f", True),
                (b"x/git-drop/visible", "f", True),
                (b"x/excluded/visible", "l", True),
                (b"x/global-drop/visible", "o", True),
                (b"x/local-drop", "d", True),
                (b"x/Plain-drop/visible", "f", False),
                (b"x/not-plain-drop/visible", "f", False),
                (b"plain-drop-file.c", "f", False),
                (b"discard.tmp", "f", True), (b".tmp", "f", True),
                (b"KEEP-ME.tmp", "f", True), (b"keep-me.tmp", "f", False),
                (b"x/keep-me.tmp", "f", False), (b"x/discard.TMP", "f", False),
                (b"discard.tmp", "d", False), (b"discard.tmp/child.c", "f", False),
                (b"discard.tmp", "l", True), (b"discard.tmp", "o", True),
            ]
            for path, kind, expected in cases:
                with self.subTest(path=path, kind=kind):
                    self.assertEqual(fixtures.ignored(dataset, path, kind), expected)
            none = fixtures.Dataset("none", Path(temporary), "test")
            for path, kind, _expected in cases:
                self.assertFalse(fixtures.ignored(none, path, kind))
            with self.assertRaises(ValueError):
                fixtures.ignored(dataset, b"../escape", "f")
            with self.assertRaises(ValueError):
                fixtures.ignored(dataset, b"valid", "unknown")


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)

    def test_layout_metadata_only_raw_paths_links_fifo_and_source_preservation(self):
        source = self.base / "source"
        source.mkdir()
        (source / "empty").mkdir()
        (source / "nested").mkdir()
        (source / "nested" / "secret.c").write_bytes(b"source data never copied")
        raw_path = os.fsencode(source) + b"/\xff\n.C"
        with open(raw_path, "wb") as stream:
            stream.write(b"more source data")
        external = self.base / "external"
        external.mkdir()
        (external / "not-captured").write_bytes(b"outside")
        os.symlink("nested", source / "safe-link")
        os.symlink(os.fspath(external), source / "external-link")
        os.symlink("../external", source / "parent-link")
        os.symlink("cycle", source / "cycle")
        os.mkfifo(source / "pipe")
        before = filesystem_nodes(os.fsencode(source))
        original_open = os.open
        source_files = {os.fsencode(source / "nested" / "secret.c"), raw_path}

        def metadata_open(path, flags, *args, **kwargs):
            if os.fsencode(path) in source_files:
                raise AssertionError("attempt to open a regular source file")
            return original_open(path, flags, *args, **kwargs)

        with mock.patch.object(fixtures.os, "open", side_effect=metadata_open):
            dataset = fixtures._layout_snapshot("layout", source, self.base / "dest")
        self.assertEqual(dataset.family, "layout")
        self.assertEqual(dataset.policy, "none")
        self.assertEqual(dataset.nodes, before)
        self.assertEqual(filesystem_nodes(dataset.root), before)
        self.assertEqual(filesystem_nodes(os.fsencode(source)), before)
        self.assertEqual((source / "nested" / "secret.c").read_bytes(), b"source data never copied")
        with open(raw_path, "rb") as stream:
            self.assertEqual(stream.read(), b"more source data")
        for path, kind in dataset.nodes.items():
            if kind == "f":
                self.assertEqual(os.lstat(dataset.root + b"/" + path).st_size, 0)
        self.assertEqual(os.readlink(dataset.root + b"/safe-link"), b"nested")
        for path in (b"external-link", b"parent-link"):
            target = os.readlink(dataset.root + b"/" + path)
            self.assertFalse(target.startswith(b"/"))
            self.assertNotIn(b"..", target.split(b"/"))
        self.assertNotIn(b"external-link/not-captured", dataset.nodes)
        self.assertEqual(dataset.provenance["source_path"], os.fspath(source))
        self.assertTrue(dataset.provenance["metadata_only"])
        self.assertFalse(dataset.provenance["follow_symlinks"])
        self.assertEqual(dataset.provenance["node_limit"], 250_000)
        captured = {bytes.fromhex(item["path_hex"]): item["kind"]
                    for item in dataset.provenance["captured_paths_manifest"]}
        self.assertEqual(captured, before)
        json.dumps(dataset.provenance)

    def test_layout_cap_and_traversal_errors_are_not_silent_partial_snapshots(self):
        source = self.base / "source"
        (source / "nested").mkdir(parents=True)
        (source / "nested" / "file").write_bytes(b"preserved")
        with mock.patch.object(fixtures, "SNAPSHOT_NODE_LIMIT", 2):
            with self.assertRaises(ValueError):
                fixtures._layout_snapshot("too-big", source, self.base / "dest")
        self.assertFalse((self.base / "dest" / "too-big").exists())
        original_scandir = os.scandir
        calls = 0

        def traversal_error(path):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise PermissionError("controlled traversal failure")
            return original_scandir(path)

        with mock.patch.object(fixtures.os, "scandir", side_effect=traversal_error):
            with self.assertRaises(PermissionError):
                fixtures._layout_snapshot("partial", source, self.base / "dest")
        self.assertFalse((self.base / "dest" / "partial").exists())
        self.assertEqual((source / "nested" / "file").read_bytes(), b"preserved")
        os.symlink(source, self.base / "source-link")
        with self.assertRaises(OSError):
            fixtures._layout_snapshot("root-link", self.base / "source-link", self.base / "dest")

    @unittest.skipUnless(shutil.which("git"), "git executable not available")
    def test_git_immutable_tracked_only_metadata_snapshot_and_no_live_content(self):
        source = self.base / "repo"
        source.mkdir()
        # Disable user/system Git configuration and hooks for this owned repo.
        environment = dict(os.environ, GIT_CONFIG_NOSYSTEM="1",
                           GIT_CONFIG_GLOBAL=os.devnull,
                           GIT_AUTHOR_NAME="Fixture Test", GIT_AUTHOR_EMAIL="fixture@example.invalid",
                           GIT_COMMITTER_NAME="Fixture Test", GIT_COMMITTER_EMAIL="fixture@example.invalid")
        for key in tuple(environment):
            if key.startswith("GIT_") and key not in (
                    "GIT_CONFIG_NOSYSTEM", "GIT_CONFIG_GLOBAL", "GIT_AUTHOR_NAME",
                    "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL"):
                del environment[key]

        def git(*args):
            return subprocess.check_output(["git", "-C", os.fspath(source),
                                            "-c", "core.hooksPath=" + os.devnull,
                                            "-c", "commit.gpgSign=false", *args],
                                           env=environment, stderr=subprocess.PIPE)

        git("init", "--quiet")
        (source / "nested").mkdir()
        (source / "nested" / "tracked.c").write_bytes(b"committed private data")
        (source / ".gitignore").write_bytes(b"ignored-untracked/\n")
        (source / "delete-later").write_bytes(b"committed, then removed")
        raw = os.fsencode(source) + b"/\xff\n.C"
        with open(raw, "wb") as stream:
            stream.write(b"committed raw path")
        os.symlink("nested/tracked.c", source / "link")
        os.symlink("../escape", source / "unsafe-link")
        git("add", "--", ".")
        git("commit", "--quiet", "-m", "Fixture snapshot")
        pinned = git("rev-parse", "HEAD").strip().decode("ascii")
        (source / "nested" / "tracked.c").write_bytes(b"changed live content")
        (source / "delete-later").unlink()
        (source / "untracked").write_bytes(b"must not appear")
        (source / "ignored-untracked").mkdir()
        (source / "ignored-untracked" / "private").write_bytes(b"not tracked")
        git("add", "-u")
        git("commit", "--quiet", "-m", "Move live head")
        self.assertNotEqual(git("rev-parse", "HEAD").strip().decode("ascii"), pinned)
        git("update-ref", "refs/heads/snapshot-ref", pinned)
        original_output = subprocess.check_output
        commands = []

        def capture_command(command, *args, **kwargs):
            commands.append(command)
            result = original_output(command, *args, **kwargs)
            if "rev-parse" in command:
                # Move the symbolic revision AFTER resolution but BEFORE tree
                # capture.  Only the resolved id may reach ls-tree.
                original_output(["git", "-C", os.fspath(source), "update-ref",
                                 "refs/heads/snapshot-ref", "HEAD"], env=environment)
            return result

        with mock.patch.object(fixtures.subprocess, "check_output", side_effect=capture_command):
            datasets = fixtures.build(self.base / "dest", True,
                                      [("git-owned", source, "snapshot-ref")], [], None)
            dataset = datasets[-1]
        expected = {b"": "d", b"nested": "d", b"nested/tracked.c": "f",
                    b".gitignore": "f", b"delete-later": "f", b"\xff\n.C": "f",
                    b"link": "l", b"unsafe-link": "l"}
        self.assertEqual(dataset.nodes, expected)
        self.assertEqual(filesystem_nodes(dataset.root), expected)
        self.assertEqual(dataset.family, "git")
        self.assertEqual(dataset.policy, "none")
        self.assertEqual(dataset.provenance["resolved_commit"], pinned)
        self.assertEqual(dataset.provenance["requested_revision"], "snapshot-ref")
        self.assertNotEqual(git("rev-parse", "snapshot-ref").strip().decode("ascii"), pinned)
        self.assertTrue(dataset.provenance["tracked_path_only"])
        self.assertTrue(dataset.provenance["metadata_only"])
        self.assertFalse(dataset.provenance["source_contents_copied"])
        self.assertEqual(len(commands), 2)
        self.assertIn("rev-parse", commands[0])
        self.assertIn("ls-tree", commands[1])
        self.assertEqual(commands[1][-1], pinned)
        for path, kind in expected.items():
            if kind == "f":
                self.assertEqual(os.lstat(dataset.root + b"/" + path).st_size, 0)
            if kind == "l":
                target = os.readlink(dataset.root + b"/" + path)
                self.assertFalse(target.startswith(b"/"))
                self.assertNotIn(b"..", target.split(b"/"))
        self.assertEqual((source / "nested" / "tracked.c").read_bytes(), b"changed live content")
        self.assertEqual((source / "untracked").read_bytes(), b"must not appear")
        self.assertFalse((source / "delete-later").exists())
        with mock.patch.object(fixtures, "SNAPSHOT_NODE_LIMIT", 2):
            with self.assertRaises(ValueError):
                fixtures._git_snapshot("capped", source, pinned, self.base / "dest")
        self.assertFalse((self.base / "dest" / "capped").exists())

    def test_public_build_includes_requested_layout_label(self):
        source = self.base / "source"
        source.mkdir()
        (source / "empty").mkdir()
        (source / "private").write_bytes(b"not copied")
        datasets = fixtures.build(self.base / "dest", True, [], [("requested-layout", source)], None)
        layout = datasets[-1]
        self.assertEqual((layout.label, layout.family), ("requested-layout", "layout"))
        self.assertEqual(layout.nodes, {b"": "d", b"empty": "d", b"private": "f"})
        self.assertEqual(layout.provenance["actual_counts"], {"d": 2, "f": 1, "l": 0, "o": 0})
        self.assertEqual(os.lstat(layout.root + b"/private").st_size, 0)


if __name__ == "__main__":
    unittest.main()
