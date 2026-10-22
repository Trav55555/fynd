"""Deterministic, metadata-only fixtures for the comprehensive benchmark.

Nothing in this module scans a source until build() is called.  Source snapshots
are names/types, not copies of source data or claims about source-content I/O.
The parent runner, not this module, supplies isolated HOME/XDG ignore settings.
"""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess


SNAPSHOT_NODE_LIMIT = 250_000
_KINDS = frozenset(("d", "f", "l", "o"))
_PRUNED = frozenset((b".git", b"git-drop", b"plain-drop", b"excluded",
                     b"global-drop", b"local-drop"))
_LINK_PLACEHOLDER = b"__snapshot_link_target__"


def _relative(path: bytes, *, empty: bool = False) -> bytes:
    if not isinstance(path, bytes):
        raise TypeError("fixture-relative paths must be bytes")
    if path == b"" and empty:
        return path
    if not path or b"\0" in path or any(p in (b"", b".", b"..")
                                           for p in path.split(b"/")):
        raise ValueError("expected a normalized, non-escaping relative path")
    return path


def _label(label: str) -> None:
    if not isinstance(label, str):
        raise TypeError("dataset label must be str")
    _relative(os.fsencode(label))
    if "/" in label:
        raise ValueError("dataset label must be one path component")


class Dataset:
    """An owned destination tree, with a full relative-byte-path manifest.

    Existing roots are rejected: a Dataset never adopts or overwrites a source.
    Symlink targets must also be normalized relative paths (no '..').  All
    creation uses no-follow directory descriptors, including existing parents.
    """

    def __init__(self, label: str, base: Path, family: str,
                 policy: str = "none", provenance: dict | None = None):
        _label(label)
        if policy not in ("none", "ignore"):
            raise ValueError("unknown fixture ignore policy")
        self.label = label
        self.base = Path(base)
        self.family = family
        self.policy = policy
        self.provenance = dict(provenance or {})
        self.root = os.fsencode(self.base / label)
        self.base.mkdir(parents=True, exist_ok=True)
        os.mkdir(self.root)
        self.nodes: dict[bytes, str] = {b"": "d"}

    def _directory_fd(self, relative: bytes) -> int:
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        fd = os.open(self.root, flags)
        prefix = b""
        try:
            for part in relative.split(b"/") if relative else ():
                prefix = prefix + b"/" + part if prefix else part
                kind = self.nodes.get(prefix)
                if kind is None:
                    os.mkdir(part, dir_fd=fd)
                elif kind != "d":
                    raise ValueError("fixture parent is not a directory")
                child = os.open(part, flags, dir_fd=fd)
                os.close(fd)
                fd = child
                self.nodes[prefix] = "d"
            return fd
        except BaseException:
            os.close(fd)
            raise

    def directory(self, relative: bytes = b"") -> None:
        relative = _relative(relative, empty=True)
        os.close(self._directory_fd(relative))

    def _parent(self, relative: bytes) -> tuple[int, bytes]:
        relative = _relative(relative)
        if relative in self.nodes:
            raise FileExistsError(os.fsdecode(relative))
        parent, _, name = relative.rpartition(b"/")
        return self._directory_fd(parent), name

    def file(self, relative: bytes, content: bytes = b"") -> None:
        if not isinstance(content, bytes):
            raise TypeError("fixture file content must be bytes")
        parent, name = self._parent(relative)
        try:
            fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         os.O_NOFOLLOW, 0o600, dir_fd=parent)
            with os.fdopen(fd, "wb") as stream:
                stream.write(content)
            self.nodes[relative] = "f"
        finally:
            os.close(parent)

    def link(self, relative: bytes, target: bytes) -> None:
        target = _relative(target)
        parent, name = self._parent(relative)
        try:
            os.symlink(target, name, dir_fd=parent)
            self.nodes[relative] = "l"
        finally:
            os.close(parent)

    def fifo(self, relative: bytes) -> None:
        parent, name = self._parent(relative)
        try:
            os.mkfifo(name, 0o600, dir_fd=parent)
            self.nodes[relative] = "o"
        finally:
            os.close(parent)


def ignored(dataset: Dataset, path: bytes, kind: str) -> bool:
    """Independent SPEC oracle, not an implementation of Git's ignore engine.

    Pruned directory names apply at every depth; non-directory '*.tmp' entries
    are excluded except the exact basename 'keep-me.tmp'.  Dot visibility is a
    separate runner policy.  Explicit roots (the empty relative path) survive.
    """
    path = _relative(path, empty=True)
    if kind not in _KINDS:
        raise ValueError("unknown node kind")
    if dataset.policy != "ignore" or not path:
        return False
    components = path.split(b"/")
    directories = components if kind == "d" else components[:-1]
    if any(component in _PRUNED for component in directories):
        return True
    name = components[-1]
    return kind != "d" and name.endswith(b".tmp") and name != b"keep-me.tmp"


def _join(parent: bytes, name: bytes) -> bytes:
    return parent + b"/" + name if parent else name


def _name(index: int, prefix: bytes = b"") -> bytes:
    hidden = b"." if index % 7 == 0 else b""
    needle = (b"Needle" if index % 22 == 0 else b"needle") if index % 11 == 0 else b"item"
    suffix = b".C" if index % 13 == 0 else (b".c" if index % 2 == 0 else b".txt")
    return hidden + prefix + needle + (b"%07d" % index) + suffix


def _files(dataset: Dataset, parent: bytes, count: int, prefix: bytes = b"") -> None:
    dataset.directory(parent)
    for index in range(count):
        dataset.file(_join(parent, _name(index, prefix)))


def _tree(dataset: Dataset, parent: bytes, fanout: int, levels: int,
          files_per_directory: int) -> None:
    """Populate every directory, including the subtree root; levels are edges."""
    _files(dataset, parent, files_per_directory)
    if levels:
        for index in range(fanout):
            _tree(dataset, _join(parent, b"d%02d" % index), fanout,
                  levels - 1, files_per_directory)


def _safe_target(target: bytes) -> bytes:
    try:
        return _relative(target)
    except ValueError:
        return _LINK_PLACEHOLDER


def _snapshot_manifest(nodes: dict[bytes, str]) -> list[dict]:
    # Hex is lossless for newline/non-UTF-8 paths and portable JSON metadata.
    return [{"path_hex": path.hex(), "kind": kind}
            for path, kind in sorted(nodes.items())]


def _capture_node(nodes: dict[bytes, str], path: bytes, kind: str) -> None:
    path = _relative(path, empty=True)
    components = path.split(b"/") if path else []
    for length in range(1, len(components)):
        parent = b"/".join(components[:length])
        previous = nodes.setdefault(parent, "d")
        if previous != "d":
            raise ValueError("snapshot has a non-directory ancestor")
    previous = nodes.setdefault(path, kind)
    if previous != kind:
        raise ValueError("snapshot has conflicting node kinds")
    if len(nodes) > SNAPSHOT_NODE_LIMIT:
        raise ValueError("snapshot exceeds 250000-node limit")


def _git_snapshot(label: str, source: Path, revision: str, base: Path) -> Dataset:
    source = Path(source).absolute()
    command = ["git", "-C", os.fspath(source)]
    # Resolve once.  ls-tree must only ever receive this immutable object id.
    commit = subprocess.check_output(command + ["rev-parse", "--verify",
                                    "--end-of-options", revision + "^{commit}"]
                                     ).strip().decode("ascii")
    if len(commit) not in (40, 64) or any(c not in "0123456789abcdef" for c in commit):
        raise ValueError("git did not resolve an immutable commit id")
    output = subprocess.check_output(command + ["ls-tree", "-r", "-t", "-z",
                                                "--full-tree", commit])
    nodes = {b"": "d"}
    gitlinks = []
    if output and not output.endswith(b"\0"):
        raise ValueError("truncated git ls-tree output")
    for record in output.split(b"\0"):
        if not record:
            continue
        header, path = record.split(b"\t", 1)
        mode, object_type, _object_id = header.split(b" ")
        if mode == b"040000" and object_type == b"tree":
            kind = "d"
        elif mode in (b"100644", b"100755") and object_type == b"blob":
            kind = "f"
        elif mode == b"120000" and object_type == b"blob":
            kind = "l"
        elif mode == b"160000" and object_type == b"commit":
            kind = "d"  # gitlinks have no tracked descendants in this tree.
            gitlinks.append(path.hex())
        else:
            raise ValueError("unsupported git tree entry")
        _capture_node(nodes, path, kind)
    provenance = {
        "source_path": os.fspath(source), "requested_revision": revision,
        "resolved_commit": commit, "metadata_only": True,
        "tracked_path_only": True, "snapshot_scope": "tracked-path-only",
        "source_contents_copied": False, "node_limit": SNAPSHOT_NODE_LIMIT,
        "symlink_targets": "safe-placeholder; no blobs read",
        "gitlinks_path_hex": gitlinks,
        "captured_paths_manifest": _snapshot_manifest(nodes),
        "captured_node_count": len(nodes),
    }
    dataset = Dataset(label, base, "git", provenance=provenance)
    _materialize(dataset, nodes, {})
    return dataset


def _layout_snapshot(label: str, source: Path, base: Path) -> Dataset:
    source = Path(source).absolute()
    nodes = {b"": "d"}
    targets: dict[bytes, bytes] = {}
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    root_fd = os.open(os.fsencode(source), flags)

    def visit(fd: int, parent: bytes) -> None:
        # Entries are metadata only.  Any stat/scandir/open/readlink failure
        # propagates: a partial traversal is never represented as a snapshot.
        with os.scandir(fd) as scan:
            entries = sorted(scan, key=lambda entry: os.fsencode(entry.name))
        for entry in entries:
            name = os.fsencode(entry.name)
            path = _join(parent, name)
            mode = entry.stat(follow_symlinks=False).st_mode
            if stat.S_ISDIR(mode):
                kind = "d"
            elif stat.S_ISREG(mode):
                kind = "f"
            elif stat.S_ISLNK(mode):
                kind = "l"
            elif stat.S_ISFIFO(mode):
                kind = "o"
            else:
                raise ValueError("layout contains unsupported socket/device node")
            _capture_node(nodes, path, kind)
            if kind == "l":
                targets[path] = _safe_target(os.readlink(name, dir_fd=fd))
            elif kind == "d":
                child = os.open(name, flags, dir_fd=fd)
                try:
                    visit(child, path)
                finally:
                    os.close(child)

    try:
        visit(root_fd, b"")
    finally:
        os.close(root_fd)
    provenance = {
        "source_path": os.fspath(source), "metadata_only": True,
        "source_contents_copied": False, "follow_symlinks": False,
        "node_limit": SNAPSHOT_NODE_LIMIT,
        "snapshot_scope": "captured-static-paths-and-types",
        "symlink_targets": "safe-relative-readlink-or-placeholder",
        "captured_paths_manifest": _snapshot_manifest(nodes),
        "captured_node_count": len(nodes),
    }
    dataset = Dataset(label, base, "layout", provenance=provenance)
    _materialize(dataset, nodes, targets)
    return dataset


def _materialize(dataset: Dataset, nodes: dict[bytes, str],
                 targets: dict[bytes, bytes]) -> None:
    for path, kind in sorted(nodes.items()):
        if kind == "d":
            dataset.directory(path)
        elif kind == "f":
            dataset.file(path)
        elif kind == "l":
            dataset.link(path, targets.get(path, _LINK_PLACEHOLDER))
        elif kind == "o":
            dataset.fifo(path)


def _raw(dataset: Dataset) -> None:
    for name in (b"ASCII.c", b"ascii.c", b"ASCII.C", b"ascii.txt", b"no-extension",
                 b".hidden.c", b"line\nbreak.c", b"high-\xff.txt", b"a-", b"z",
                 b"space name.c", b"tab\tname.txt", b"quote'back\\slash.c"):
        dataset.file(name)
    dataset.file(b"a/z")
    dataset.file(b".hidden-dir/visible.c")
    for index in range(40):
        dataset.directory(b"dir-%02d" % index)
    dataset.file(b"dir-00/child.c")
    dataset.link(b"file-link", b"ASCII.c")
    dataset.link(b"dir-link", b"dir-00")
    dataset.link(b"dangling-link", b"absent")
    dataset.link(b"loop-link", b"loop-link")
    dataset.fifo(b"named-pipe")


def _ignore(dataset: Dataset, count: int, quick: bool) -> None:
    dataset.file(b".git/info/exclude", b"excluded/\n")
    dataset.file(b".gitignore", b"git-drop/\n*.tmp\n!keep-me.tmp\n")
    misses = b"".join(b"never-match-%04d-*.absent\n" % i for i in range(count))
    dataset.file(b".ignore", b".git/\nplain-drop/\n" + misses)
    dataset.file(b"keep/d00/.gitignore", b"local-drop/\n")
    fanout, levels, files = (2, 2, 3) if quick else (6, 4, 16)
    for parent in (b"git-drop", b"plain-drop", b"excluded", b"global-drop",
                   b"keep/d00/local-drop"):
        _tree(dataset, parent, fanout, levels, files)
        dataset.file(_join(parent, b"keep-me.tmp"))
    # Root .ignore wins over Git ignore sources at every depth.  Also keep
    # similarly named ordinary files: pruning is about directories, not names.
    for parent in (b"", b"keep", b"keep/d00", b"keep/d00/kept"):
        _files(dataset, parent, 8 if quick else 32)
        dataset.file(_join(parent, b"drop.tmp"))
        dataset.file(_join(parent, b"keep-me.tmp"))
    for parent in (b"keep/git-drop", b"keep/plain-drop", b"keep/excluded",
                   b"keep/global-drop"):
        dataset.file(_join(parent, b"dropped.c"))
    dataset.file(b"keep/plain-drop-file.c")
    dataset.file(b"plain-drop-file.c")
    dataset.directory(b"keep/empty")
    if count:
        for index in range(3 if quick else 16):
            _files(dataset, b"keep/match%02d" % index, 7 if quick else 384)
    dataset.provenance["parameters"] = {
        "miss_only_patterns": count, "drop_fanout": fanout,
        "drop_levels": levels, "drop_files_per_directory": files,
        "global_ignore_required": "global-drop/\n",
        "global_ignore_owner": "parent isolated HOME/XDG",
    }


def _finish(dataset: Dataset) -> None:
    counts = {kind: 0 for kind in ("d", "f", "l", "o")}
    for kind in dataset.nodes.values():
        counts[kind] += 1
    dataset.provenance["actual_nodes"] = len(dataset.nodes)
    dataset.provenance["actual_counts"] = counts
    dataset.provenance["actual_extension_counts"] = {
        os.fsdecode(extension): sum(kind == "f" and path.endswith(extension)
                                    for path, kind in dataset.nodes.items())
        for extension in (b".c", b".C", b".txt", b".tmp")
    }
    dataset.provenance["actual_name_counts"] = {
        "hidden": sum(bool(path) and path.rsplit(b"/", 1)[-1].startswith(b".")
                      for path in dataset.nodes),
        "needle": sum(b"needle" in path.rsplit(b"/", 1)[-1]
                      for path in dataset.nodes),
        "Needle": sum(b"Needle" in path.rsplit(b"/", 1)[-1]
                      for path in dataset.nodes),
    }


def build(base: Path, quick: bool, git_trees: list[tuple[str, Path, str]],
          layouts: list[tuple[str, Path]], secondary: Path | None) -> list[Dataset]:
    """Create all required shapes.  Caller owns destinations and their cleanup.

    Quick mode changes counts, never labels/families or raw/ignore features.
    Tree parameters count files in every directory, including the subtree root.
    Full output names alone exceed 1 MiB per output directory.
    """
    base = Path(base)
    labels = ["empty", "tiny", "flat10k", "flat100k", "flat1m", "deep",
              "balanced", "manydirs", "prefix", "skew", "output",
              "sharedprefix", "raw", "ignore-prune", "ignore64", "ignore512"]
    if secondary is not None:
        labels += ["ram-flat", "ram-manydirs"]
    labels += [label for label, _path, _revision in git_trees]
    labels += [label for label, _path in layouts]
    for label in labels:
        _label(label)
    if len(set(labels)) != len(labels):
        raise ValueError("dataset labels must be unique")
    datasets: list[Dataset] = []

    def new(label: str, family: str, *, policy: str = "none",
            destination: Path = base, **parameters: int) -> Dataset:
        dataset = Dataset(label, destination, family, policy,
                          {"quick": quick, "parameters": parameters,
                           "source_contents_copied": False})
        datasets.append(dataset)
        return dataset

    new("empty", "empty")
    _files(new("tiny", "tiny", files=5), b"", 5)
    for label, full, small in (("flat10k", 10_000, 32),
                               ("flat100k", 100_000, 64),
                               ("flat1m", 1_000_000, 128)):
        count = small if quick else full
        _files(new(label, "flat", files=count), b"", count)

    levels, files = (6, 4) if quick else (96, 32)
    dataset = new("deep", "deep", levels=levels, files_per_directory=files)
    parent = b""
    for level in range(levels):
        parent = _join(parent, b"d%03d" % level)
        _files(dataset, parent, files)

    for label, full_levels, full_files in (("balanced", 5, 32), ("manydirs", 6, 4)):
        fanout, levels, files = (2, 3, 3) if quick else (6, full_levels, full_files)
        _tree(new(label, label, fanout=fanout, levels=levels,
                  files_per_directory=files), b"", fanout, levels, files)

    chain, fanout, levels, files = (3, 2, 3, 3) if quick else (8, 6, 5, 12)
    dataset = new("prefix", "prefix", chain_levels=chain, fork_fanout=fanout,
                  fork_levels=levels, files_per_directory=files)
    parent = b""
    for level in range(chain):
        parent = _join(parent, b"chain%02d" % level)
        dataset.directory(parent)
    _tree(dataset, parent, fanout, levels, files)

    big, small = (64, 2) if quick else (100_000, 8)
    dataset = new("skew", "skew", big_files=big, tiny_branches=7, tiny_files=small)
    _files(dataset, b"d00-big", big)
    for index in range(1, 8):
        _files(dataset, b"d%02d-tiny" % index, small)

    files = 12 if quick else 6000
    dataset = new("output", "output", directories=8, files_per_directory=files,
                  name_prefix_bytes=200)
    for index in range(8):
        _files(dataset, b"d%02d" % index, files, b"long-name-" + b"x" * 190)

    files = 32 if quick else 20_000
    _files(new("sharedprefix", "sharedprefix", files=files, name_prefix_bytes=200),
           b"", files, b"s" * 200)
    _raw(new("raw", "raw", empty_directories=39, raw_directories=40))
    for label, count in (("ignore-prune", 0), ("ignore64", 64), ("ignore512", 512)):
        _ignore(new(label, "ignore", policy="ignore"), count, quick)

    for label, source, revision in git_trees:
        dataset = _git_snapshot(label, source, revision, base)
        dataset.provenance["quick"] = quick
        datasets.append(dataset)
    for label, source in layouts:
        dataset = _layout_snapshot(label, source, base)
        dataset.provenance["quick"] = quick
        datasets.append(dataset)
    if secondary is not None:
        count = 64 if quick else 100_000
        _files(new("ram-flat", "ram", destination=Path(secondary), files=count),
               b"", count)
        fanout, levels, files = (2, 3, 2) if quick else (6, 5, 4)
        _tree(new("ram-manydirs", "ram", destination=Path(secondary),
                  fanout=fanout, levels=levels, files_per_directory=files),
              b"", fanout, levels, files)
    for dataset in datasets:
        _finish(dataset)
    return datasets
