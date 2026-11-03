#!/usr/bin/env python3
"""Build native compiler arithmetic helpers only when their inputs change."""
import hashlib
from pathlib import Path
import subprocess
import sys

repo, source = map(Path, sys.argv[1:])
compiler = source / "third_party/llvm-build/Release+Asserts/bin"
runtime = compiler.parent / "lib/clang/24/lib/charmos"
runtime.mkdir(parents=True, exist_ok=True)
builtins = source / "third_party/llvm/compiler-rt/lib/builtins"
units = ("udivti3", "umodti3", "udivmodti4")
inputs = [Path(__file__), repo / "tools/chromium/upstream.json"]
inputs += [builtins / (name + ".c") for name in units]
inputs += sorted(builtins.glob("*.h"))
inputs += sorted(path for path in (compiler.parent / "lib/clang/24/include").rglob("*") if path.is_file())
inputs += sorted(path for path in (repo / "user/libc/include").rglob("*") if path.is_file())
digest = hashlib.sha256()
for path in inputs:
    digest.update(str(path).encode())
    digest.update(b"\0")
    digest.update(path.read_bytes())
for name in ("clang", "llvm-ar"):
    metadata = (compiler / name).stat()
    digest.update(f"{name}:{metadata.st_size}:{metadata.st_mtime_ns}".encode())
signature = digest.hexdigest() + "\n"
stamp = runtime / "builtins-inputs.sha256"
archive = runtime / "libclang_rt.builtins.a"
objects = [runtime / (name + ".o") for name in units]
if stamp.exists() and stamp.read_text() == signature and archive.exists() and all(path.exists() for path in objects):
    raise SystemExit(0)
for name, output in zip(units, objects):
    temporary = output.with_suffix(".o.tmp")
    subprocess.run([str(compiler / "clang"), "--target=x86_64-unknown-elf", "-O2",
                    "-ffreestanding", "-nostdlibinc", "-isystem", str(repo / "user/libc/include"),
                    "-c", str(builtins / (name + ".c")), "-o", str(temporary)], check=True)
    temporary.replace(output)
temporary_archive = archive.with_suffix(".a.tmp")
temporary_archive.unlink(missing_ok=True)
subprocess.run([str(compiler / "llvm-ar"), "rcs", str(temporary_archive), *map(str, objects)], check=True)
temporary_archive.replace(archive)
temporary_stamp = stamp.with_suffix(".sha256.tmp")
temporary_stamp.write_text(signature)
temporary_stamp.replace(stamp)
