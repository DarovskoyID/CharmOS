#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
python3 "$root/tools/chromium/prepare-platform.py"
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["chromium"]["version"])' "$root/tools/chromium/upstream.json")"
source="$cache/release/chromium-$version"
# Native compiler ABI helpers. Other unsupported runtime libraries remain
# missing link dependencies; they are not substituted with Linux binaries.
compiler="$source/third_party/llvm-build/Release+Asserts/bin"
runtime="$source/third_party/llvm-build/Release+Asserts/lib/clang/24/lib/charmos"
mkdir -p "$runtime"
for unit in udivti3 umodti3 udivmodti4; do
  "$compiler/clang" --target=x86_64-unknown-elf -O2 -ffreestanding -nostdlibinc \
    -isystem "$root/user/libc/include" -c "$source/third_party/llvm/compiler-rt/lib/builtins/$unit.c" \
    -o "$runtime/$unit.o"
done
"$compiler/llvm-ar" rcs "$runtime/libclang_rt.builtins.a" "$runtime"/*.o
output="$source/out/charmos-platform"
mkdir -p "$output"
python3 - "$root" "$output/args.gn" "$source/third_party/llvm-build/Release+Asserts" <<'PY'
import json, pathlib, sys
args = {
    "target_os": "charmos", "target_cpu": "x64", "charmos_repo": sys.argv[1],
    "is_debug": False, "is_component_build": False, "symbol_level": 1,
    "enable_rust": False, "use_sysroot": False, "use_remoteexec": False, "use_glib": False,
    "use_udev": False, "use_dbus": False,
    "use_ozone": True, "ozone_auto_platforms": False,
    "ozone_platform_headless": True, "clang_use_chrome_plugins": False,
    # The source archive has an empty release stamp. Register the actual
    # compiler explicitly as custom, rather than inventing a matching stamp.
    "clang_base_path": sys.argv[3],
}
destination = pathlib.Path(sys.argv[2])
content = "".join(f"{k} = {json.dumps(v)}\n" for k,v in args.items())
if not destination.exists() or destination.read_text() != content:
    destination.write_text(content)
PY
cd "$source"
buildtools/linux64/gn gen "$output" --fail-on-unused-args --root-target=//build/charmos:base_port
ninja -C "$output" -k 0 -j 4 platform_smoke
"$compiler/llvm-objcopy" --strip-debug "$output/platform_smoke.elf" "$root/build/user/user/platform_smoke.elf"
python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/platform_smoke.elf"
python3 - "$root" "$source" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, source = map(pathlib.Path, sys.argv[1:])
elf = root / "build/user/user/platform_smoke.elf"
report = {
    "scope": "GN bootstrap executable; full libc++/base/browser not completed",
    "upstream": json.loads((root / "tools/chromium/upstream.json").read_text())["chromium"],
    "compiler": subprocess.check_output([str(source / "third_party/llvm-build/Release+Asserts/bin/clang"), "--version"], text=True),
    "compiler_stamp": (source / "third_party/llvm-build/Release+Asserts/cr_build_revision").read_text().strip(),
    "toolchain_revision": (source / "build/charmos/toolchain-revision.gni").read_text().strip(),
    "args": (source / "out/charmos-platform/args.gn").read_text(),
    "elf_bytes": elf.stat().st_size,
    "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
}
(root / "build/chromium/platform-report.json").write_text(json.dumps(report, indent=2) + "\n")
PY
if [[ "${1:-}" == --full-runtime ]]; then
  ninja -C "$output" -k 0 -j 4 full_runtime
fi
