#!/usr/bin/env python3
"""Native target toolchain; host generators use Chromium's Linux toolchain."""
import json
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
version = json.loads((repo / "tools/chromium/upstream.json").read_text())["chromium"]["version"]
source = repo / "build/chromium/linux-cache/release" / ("chromium-" + version)
compiler = source / "third_party/llvm-build/Release+Asserts/bin"
mode, *arguments = sys.argv[1:]
if mode == "ar":
    raise SystemExit(subprocess.call([str(compiler / "llvm-ar"), *arguments]))
if mode not in ("cc", "cxx", "link"):
    raise SystemExit("Unknown CharmOS tool mode: " + mode)
flags = ["--target=x86_64-unknown-elf", "-ffreestanding", "-fno-stack-protector",
         "-fno-pic", "-fno-pie", "-D__charmos__=1", "-nostdlibinc",
         "-isystem", str(repo / "user/libc/include"),
         "-I" + str(repo / "user/rt/include"), "-I" + str(repo / "include")]
if mode != "link":
    # These are system headers for diagnostics, but remain native port inputs.
    flags += ["-MD"]
if mode != "cc":
    flags += ["-nostdinc++",
              "-isystem", str(source / "buildtools/third_party/libc++"),
              "-isystem", str(source / "third_party/libc++/src/include"),
              "-isystem", str(source / "third_party/libc++abi/src/include")]
if mode == "link":
    if any(arg in ("-shared", "-pie", "-Wl,-pie", "-Wl,-shared") for arg in arguments):
        raise SystemExit("CharmOS dynamic/PIE linking is not implemented")
    flags += ["-nostdlib", "-static", "-fuse-ld=lld",
              "-Wl,--no-dynamic-linker", "-Wl,-z,max-page-size=4096",
              "-T", str(repo / "user/rt/user.ld"),
              "-Wl,--start-group", str(repo / "build/user/user/libc.a"), "-Wl,--end-group"]
tool = compiler / ("clang" if mode == "cc" else "clang++")
result = subprocess.call([str(tool), *arguments, *flags])
if result == 0 and mode == "link":
    if "-o" not in arguments:
        raise SystemExit("CharmOS link requires an explicit output")
    output = Path(arguments[arguments.index("-o") + 1])
    result = subprocess.call([sys.executable, str(repo / "tools/chromium/audit-elf.py"), str(output)])
    if result:
        output.unlink(missing_ok=True)
raise SystemExit(result)
