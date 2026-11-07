"""Exercise the real Ninja link edge against an external libc timestamp."""
import os
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]).resolve()
archive = repo / "build/user/user/libc.a"
elf = output / "v8_runtime.elf"
if "--dependency-file=" not in (output / "toolchain.ninja").read_text():
    raise SystemExit("Native linker rule does not record its dependencies")
if "--dependency-file=" in (output / "clang_x64/toolchain.ninja").read_text():
    raise SystemExit("Native linker dependency flag leaked into the host toolchain")
# GN enables Ninja's gcc dependency database; Ninja consumes/removes the .d
# file after a successful link. Inspect the persisted edge, not that temporary.
dependencies = subprocess.run(
    ["ninja", "-C", str(output), "-t", "deps", "v8_runtime.elf"],
    capture_output=True, text=True, check=True).stdout
for dependency in (archive, repo / "user/rt/user.ld"):
    if str(dependency) not in dependencies:
        raise SystemExit("Missing real linker dependency: " + str(dependency))
original = archive.stat()
try:
    # Only the archive becomes newer; object files and the ELF stay untouched.
    os.utime(archive, ns=(original.st_atime_ns, elf.stat().st_mtime_ns + 1_000_000_000))
    result = subprocess.run(["ninja", "-C", str(output), "-n", "-d", "explain", "v8_runtime"],
                            capture_output=True, text=True, check=True)
    diagnostics = result.stdout + result.stderr
    if "libc.a" not in diagnostics or "LINK ./v8_runtime.elf" not in diagnostics:
        raise SystemExit("External libc change did not invalidate the executable:\n" + diagnostics)
    if " CXX " in result.stdout or " CC " in result.stdout:
        raise SystemExit("Archive-only change unnecessarily invalidated C++ objects:\n" + diagnostics)
finally:
    os.utime(archive, ns=(original.st_atime_ns, original.st_mtime_ns))
print("Native link dependencies: archive change schedules relink without C++ rebuild")
