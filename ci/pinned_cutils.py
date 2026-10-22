#!/usr/bin/env python3
"""Check the c-utils content pin, or compile from a private verified snapshot.

The manifest and project revision are trusted build policy. Compiler arguments,
application sources, and same-user writes are not a security sandbox boundary.
"""

import argparse
from contextlib import contextmanager, nullcontext
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


class PinError(Exception):
    """Invalid policy or dependency input; no compiler may consume it."""


class BuildError(Exception):
    """Compiler output or publication failure, not a pin rejection."""


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise PinError("duplicate manifest key: " + key)
        result[key] = value
    return result


def load_pin():
    try:
        revision = (ROOT / "CUTILS_REVISION").read_text(encoding="ascii").strip()
        with (ROOT / "ci/pinned_cutils.json").open(encoding="utf-8") as stream:
            text = stream.read(65537)
        if len(text) > 65536:
            raise PinError("manifest is too large")
        policy = json.loads(text, object_pairs_hook=unique_object)
    except (OSError, UnicodeError, ValueError, RecursionError) as error:
        raise PinError("cannot read pin policy: " + str(error)) from error
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise PinError("CUTILS_REVISION must contain a complete lowercase commit ID")
    if not isinstance(policy, dict) or set(policy) != {"schema", "revision", "files"}:
        raise PinError("manifest must contain only schema, revision and files")
    if policy["schema"] != "fynd.cutils-pin/1":
        raise PinError("unsupported manifest schema")
    if policy["revision"] != revision:
        raise PinError("manifest revision does not match CUTILS_REVISION")
    files = policy["files"]
    if not isinstance(files, dict) or len(files) != 16 or "include/cu.h" not in files:
        raise PinError("manifest requires include/cu.h and exactly 15 C sources")
    for name, entry in files.items():
        if name != "include/cu.h" and not re.fullmatch(r"src/cu_[a-z0-9_]+\.c", name):
            raise PinError("invalid manifest path: " + name)
        if not isinstance(entry, dict) or set(entry) != {"sha256", "size"}:
            raise PinError("invalid manifest entry: " + name)
        if not isinstance(entry["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise PinError("invalid SHA-256: " + name)
        if type(entry["size"]) is not int or entry["size"] <= 0:
            raise PinError("invalid byte size: " + name)
    return files


@contextmanager
def directory(name, parent=None):
    # The selected root is resolved once; directories below it must not be links.
    if not stat.S_ISDIR(os.stat(name, dir_fd=parent, follow_symlinks=False).st_mode):
        raise PinError("not a real directory: " + str(name))
    flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(name, flags, dir_fd=parent)
    try:
        if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
            raise PinError("not a directory: " + str(name))
        yield descriptor
    finally:
        os.close(descriptor)


def inventory(descriptor, folder, files):
    expected = {name.split("/")[1] for name in files if name.startswith(folder + "/")}
    actual = set(os.listdir(descriptor))
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise PinError("{} inventory mismatch (missing: {}; extra: {})".format(
            folder, ", ".join(missing) or "none", ", ".join(extra) or "none"))


def consume(descriptor, name, entry, destination):
    leaf = name.split("/")[1]
    if not stat.S_ISREG(os.stat(leaf, dir_fd=descriptor, follow_symlinks=False).st_mode):
        raise PinError("not a regular, non-symlink file: " + name)
    flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
    source_fd = os.open(leaf, flags, dir_fd=descriptor)
    with os.fdopen(source_fd, "rb", buffering=0) as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise PinError("not a regular file: " + name)
        if info.st_size != entry["size"]:
            raise PinError("byte size mismatch: " + name)
        sink = destination.open("xb") if destination is not None else nullcontext(None)
        with sink as target:
            digest = hashlib.sha256()
            count = 0
            # At most the known size plus one byte, even if a writer grows it.
            while True:
                chunk = source.read(min(65536, entry["size"] + 1 - count))
                if not chunk:
                    break
                count += len(chunk)
                if count > entry["size"]:
                    raise PinError("byte size mismatch: " + name)
                digest.update(chunk)
                if target is not None:
                    target.write(chunk)
            if count != entry["size"]:
                raise PinError("byte size mismatch: " + name)
            if digest.hexdigest() != entry["sha256"]:
                raise PinError("SHA-256 mismatch: " + name)


def verify(cutils, files, snapshot=None):
    try:
        root = Path(cutils).resolve(strict=True)
        with directory(root) as root_fd:
            for folder in ("include", "src"):
                with directory(folder, root_fd) as folder_fd:
                    inventory(folder_fd, folder, files)
                    for name in sorted(files):
                        if name.startswith(folder + "/"):
                            destination = snapshot / name if snapshot is not None else None
                            consume(folder_fd, name, files[name], destination)
                    inventory(folder_fd, folder, files)
    except (OSError, ValueError, RuntimeError) as error:
        raise PinError("cannot verify dependency: " + str(error)) from error


def build(cutils, output, compiler, files):
    output = Path(os.path.abspath(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    # Staging beside the destination guarantees same-filesystem publication.
    with tempfile.TemporaryDirectory(prefix=".pinned-cutils-", dir=output.parent) as temporary:
        private = Path(temporary)
        snapshot = private / "dependency"
        (snapshot / "include").mkdir(parents=True)
        (snapshot / "src").mkdir()
        verify(cutils, files, snapshot)
        binary = private / "binary"
        sources = [str(snapshot / name) for name in sorted(files) if name.startswith("src/")]
        command = compiler + ["-I" + str(snapshot / "include")] + sources + ["-o", str(binary)]
        # Only the private snapshot reaches the compiler. Changes to the original
        # dependency after this point do not invalidate these verified bytes.
        try:
            result = subprocess.run(command, check=False)
        except OSError as error:
            raise BuildError("cannot execute compiler: " + str(error)) from error
        if result.returncode:
            return result.returncode if result.returncode > 0 else 128 - result.returncode
        try:
            info = binary.lstat()
        except FileNotFoundError as error:
            raise BuildError("compiler succeeded without producing an output") from error
        if not stat.S_ISREG(info.st_mode):
            raise BuildError("compiler output is not a regular, non-symlink file")
        os.replace(binary, output)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    check_parser = modes.add_parser("check")
    check_parser.add_argument("--cutils", required=True)
    build_parser = modes.add_parser("build")
    build_parser.add_argument("--cutils", required=True)
    build_parser.add_argument("--output", required=True)
    # Split explicitly: compiler flags must never be parsed as helper options.
    arguments = sys.argv[1:]
    compiler = []
    if "--" in arguments:
        boundary = arguments.index("--")
        compiler = arguments[boundary + 1:]
        arguments = arguments[:boundary]
    options = parser.parse_args(arguments)
    if options.mode == "build" and not compiler:
        parser.error("build requires -- followed by a compiler argument vector")
    if options.mode == "check" and compiler:
        parser.error("check does not accept compiler arguments")
    try:
        files = load_pin()
        if options.mode == "check":
            verify(options.cutils, files)
            return 0
        return build(options.cutils, options.output, compiler, files)
    except PinError as error:
        print("c-utils pin: " + str(error), file=sys.stderr)
    except (BuildError, OSError) as error:
        print("pinned build: " + str(error), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
