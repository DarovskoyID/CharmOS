"""Receipts for a verified, unchanged GN/Ninja dependency graph.

C++ edits do not change a graph. Cache reuse requires identical GN inputs,
all recursively included Ninja manifests, generation stamp, arguments and
omitted-Rust metadata. Content hashes detect even edits with restored mtime.
Unknown manifest syntax disables caching; the full audit remains mandatory.
"""
import os
import hashlib
from pathlib import Path
import re
import shlex
import stat
import json


def _manifest_name(value):
    value = re.sub(r"\$([$ :])", lambda match: match[1], value.strip())
    if "$" in value or os.path.isabs(value):
        raise ValueError("Unsupported Ninja include path")
    value = os.path.normpath(value)
    if value == ".." or value.startswith("../"):
        raise ValueError("Ninja include outside the output tree")
    return value


def snapshot(output, manifests=None):
    output = Path(output)
    depfile = (output / "build.ninja.d").read_text()
    target, separator, dependencies = depfile.partition(":")
    if not separator or target.strip() != "build.ninja.stamp":
        raise ValueError("Unsupported GN depfile")
    inputs = shlex.split(dependencies.replace("\\\n", ""))
    if not inputs:
        raise ValueError("Empty GN input list")
    if manifests is None:
        found, pending = set(), ["build.ninja"]
        while pending:
            name = _manifest_name(pending.pop())
            if name in found:
                continue
            found.add(name)
            for line in (output / name).read_text().splitlines():
                match = re.match(r"^\s*(?:subninja|include)\s+(.+)$", line)
                if match:
                    pending.append(_manifest_name(match[1]))
        manifests = sorted(found)
    else:
        manifests = sorted({_manifest_name(name) for name in manifests})
        if "build.ninja" not in manifests:
            raise ValueError("Missing root Ninja manifest")
    paths = set(inputs) | set(manifests) | {
        "args.gn", "build.ninja.d", "build.ninja.stamp", "charmos-omitted-rust.json"
    }
    # Relative stat calls avoid repeatedly resolving the Windows mount prefix.
    directory = os.open(output, os.O_RDONLY | os.O_DIRECTORY)
    try:
        records = {}
        for path in sorted(paths):
            state = os.stat(path, dir_fd=directory)
            descriptor = os.open(path, os.O_RDONLY, dir_fd=directory)
            if stat.S_ISDIR(state.st_mode):
                # GN records directories to notice added/removed inputs. Each
                # file whose contents it reads is a separate depfile input.
                try:
                    with os.scandir(descriptor) as children:
                        entries = sorted((entry.name,
                                          entry.is_dir(follow_symlinks=False),
                                          os.readlink(entry.name, dir_fd=descriptor)
                                          if entry.is_symlink() else None)
                                         for entry in children)
                    digest = "directory:" + hashlib.sha256(
                        json.dumps(entries, ensure_ascii=True).encode()).hexdigest()
                finally:
                    os.close(descriptor)
            else:
                with os.fdopen(descriptor, "rb") as contents:
                    digest = "file:" + hashlib.file_digest(contents, "sha256").hexdigest()
            records[path] = [state.st_size, state.st_mtime_ns, state.st_ctime_ns,
                             state.st_ino, state.st_dev, digest]
    finally:
        os.close(directory)
    return {"manifests": manifests, "files": records}


def has_current_receipt(output, reports):
    """Prove that --resume need not regenerate GN before running the audits."""
    output = Path(output)
    for name in reports:
        try:
            receipt = json.loads((output / name).read_text())["graph_snapshot"]
            if receipt == snapshot(output, receipt["manifests"]):
                return True
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return False


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 4 or sys.argv[1] != "--is-current":
        raise SystemExit("usage: graph_cache.py --is-current OUTPUT REPORT [...]")
    raise SystemExit(0 if has_current_receipt(sys.argv[2], sys.argv[3:]) else 1)
