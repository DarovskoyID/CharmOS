#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
mode="${1:-}"
case "$mode" in ''|--full-runtime|--json-runtime|--base|--base-graph|--graphics|--graphics-graph) ;; *) echo 'Expected a runtime, base or graphics mode' >&2; exit 2;; esac
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
if [[ "$mode" != --base-graph && "$mode" != --graphics-graph ]]; then
python3 "$root/tools/chromium/build-builtins.py" "$root" "$source"
fi
output="$source/out/charmos-platform"
mkdir -p "$output"
python3 - "$root" "$output/args.gn" "$source/third_party/llvm-build/Release+Asserts" "$mode" <<'PY'
import json, pathlib, sys
args = {
    "target_os": "charmos", "target_cpu": "x64", "charmos_repo": sys.argv[1],
    "is_debug": False, "is_component_build": False, "symbol_level": 1,
    "enable_rust": False, "use_sysroot": False, "use_remoteexec": False, "use_glib": False,
    "use_udev": False, "use_dbus": False,
    # All probes share one native configuration. Selecting a diagnostic target
    # must not toggle allocator/buildflags and invalidate unrelated objects.
    "charmos_build_base": True,
    # Adds a root only; native base/compiler configuration stays unchanged.
    "charmos_build_graphics": sys.argv[4] in ("--graphics", "--graphics-graph"),
    "use_partition_alloc": False, "use_allocator_shim": False,
    "absl_build_tests": False,
    "enable_perfetto_ipc": False,
    "enable_perfetto_platform_services": False,
    "enable_perfetto_trace_processor_httpd": False,
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
if [[ "$mode" == --base || "$mode" == --base-graph ]]; then
  python3 "$root/tools/chromium/audit-cpp-graph.py" "$source" "$output"
fi
if [[ "$mode" == --base-graph ]]; then exit 0; fi
if [[ "$mode" == --graphics || "$mode" == --graphics-graph ]]; then
  python3 "$root/tools/chromium/audit-cpp-graph.py" "$source" "$output" graphics_runtime
fi
if [[ "$mode" == --graphics-graph ]]; then exit 0; fi
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
if [[ "$mode" == --full-runtime ]]; then
  ninja -C "$output" -k 0 -j 4 full_runtime
  "$compiler/llvm-objcopy" --strip-debug "$output/full_runtime.elf" "$root/build/user/user/runtime_cpp.elf"
  python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/runtime_cpp.elf"
fi
if [[ "$mode" == --json-runtime ]]; then
  ninja -C "$output" -k 0 -j 4 json_runtime
  "$compiler/llvm-objcopy" --strip-debug "$output/json_runtime.elf" "$root/build/user/user/runtime_json.elf"
  python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/runtime_json.elf"
fi
if [[ "$mode" == --base ]]; then
  ninja -C "$output" -k 0 -j 4 base_runtime
  "$compiler/llvm-objcopy" --strip-debug "$output/base_runtime.elf" "$root/build/user/user/base_runtime.elf"
  python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/base_runtime.elf"
fi
if [[ "$mode" == --graphics ]]; then
  ninja -C "$output" -k 0 -j 4 graphics_runtime
  "$compiler/llvm-objcopy" --strip-debug "$output/graphics_runtime.elf" "$root/build/user/user/graphics_runtime.elf"
  python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/graphics_runtime.elf"
fi
