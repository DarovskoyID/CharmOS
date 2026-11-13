#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
mode="${1:-}"
case "$mode" in ''|--full-runtime|--json-runtime|--base|--base-graph|--graphics|--graphics-graph|--ozone-graph|--v8|--v8-graph|--blink|--blink-graph) ;; *) echo 'Expected a runtime, base, graphics, v8 or blink mode' >&2; exit 2;; esac
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
if [[ "$mode" != --base-graph && "$mode" != --graphics-graph && "$mode" != --ozone-graph && "$mode" != --v8-graph && "$mode" != --blink-graph ]]; then
python3 "$root/tools/chromium/build-builtins.py" "$root" "$source"
fi
output="$source/out/charmos-native"
mkdir -p "$output"
python3 - "$root" "$output/args.gn" "$source/third_party/llvm-build/Release+Asserts" "$mode" <<'PY'
import json, pathlib, sys
args = {
    "target_os": "charmos", "target_cpu": "x64", "charmos_repo": sys.argv[1],
    "is_debug": False, "is_component_build": False, "symbol_level": 1, "use_blink": True,
    "enable_rust": False, "use_sysroot": False, "use_remoteexec": False, "use_glib": False,
    "use_udev": False, "use_dbus": False, "use_gio": False, "use_xkbcommon": False,
    "use_pangocairo": False,
    # All probes share one native configuration. Selecting a diagnostic target
    # must not toggle allocator/buildflags and invalidate unrelated objects.
    "charmos_build_base": True,
    # Adds a root only; native base/compiler configuration stays unchanged.
    "charmos_build_graphics": sys.argv[4] in ("--graphics", "--graphics-graph", "--ozone-graph"),
    "charmos_build_v8": sys.argv[4] in ("--v8", "--v8-graph"),
    "charmos_build_blink": sys.argv[4] in ("--blink", "--blink-graph", "--graphics", "--graphics-graph", "--ozone-graph"),
    "use_partition_alloc": True, "use_allocator_shim": False, "use_crash_key_stubs": True,
    "enable_backup_ref_ptr_support": False,
    "absl_build_tests": False,
    "enable_perfetto_ipc": False,
    "enable_perfetto_platform_services": False,
    "enable_perfetto_trace_processor_httpd": False,
    "enable_library_cdms": False, "enable_widevine": False,
    "pdf_enable_rust_png": False, "pdf_enable_fontations": False,
    "enable_supervised_users": True, "enable_webui_ntp": True,
    "use_nss_certs": False, "use_nss_server_certs": False, "use_nss_client_certs": False,
    "chrome_root_store_only": True,
    "use_cups": False, "enable_screen_ai_service": False, "enterprise_data_controls": True,
    "enterprise_watermark": True, "enterprise_cache_encryption": True,
    "enterprise_cloud_content_analysis": True,
    "enable_vulkan": False, "enable_vr": False,
    "use_ozone": True, "use_aura": True, "toolkit_views": True, "ozone_auto_platforms": False,
    "ozone_platform": "charmos", "ozone_extra_path": "//build/charmos/ozone_extra.gni",
    "ozone_platform_headless": False, "clang_use_chrome_plugins": False,
    # The source archive has an empty release stamp. Register the actual
    # compiler explicitly as custom, rather than inventing a matching stamp.
    "clang_base_path": sys.argv[3],
}
destination = pathlib.Path(sys.argv[2])
# V8: drop the Rust Temporal bridge, avoid the large pointer-compression cage,
# and skip WASM trap/signal handling for the initial native JS port.
if sys.argv[4] in ("--v8", "--v8-graph", "--blink", "--blink-graph", "--graphics", "--graphics-graph", "--ozone-graph"):
    args.update({
        "v8_enable_temporal_support": False,
        "v8_enable_pointer_compression": False,
        "v8_enable_webassembly": False,
        "v8_use_external_startup_data": False,
        # Oilpan/cppgc's caged heap reserves ~32 GiB of aligned address space
        # with MAP_FIXED-style placement. CharmOS has no fixed-address mmap and
        # the embedder does not use cppgc, so keep the cage (and implicitly
        # cppgc pointer compression) disabled.
        "cppgc_enable_caged_heap": False,
        "cppgc_enable_larger_cage": False,
    })
# Blink: no Rust. JXL is Rust-only; XML uses the C++/libxml2 parser via
# blink-patches.py. Blink needs V8 and cppgc, so the V8 profile above applies.
if sys.argv[4] in ("--blink", "--blink-graph", "--graphics", "--graphics-graph", "--ozone-graph"):
    args.update({
        "enable_jxl_decoder": False,
        "media_use_symphonia": False,
        "enable_dav1d_decoder": False,
        # No GPU/WebGPU in the software-only port.
        "use_dawn": False,
        "skia_use_dawn": False,
        "build_dawn_tests": False,
        # Software rasterization is not needed; avoid the SwiftShader/Subzero
        # toolchain, which has no CharmOS platform layer.
        "enable_swiftshader": False,
        "enable_swiftshader_vulkan": False,
        # ANGLE's WebGPU backend pulls Dawn native and SwiftShader; the software
        # port has no GPU process to use them.
        "angle_enable_wgpu": False,
        "angle_enable_swiftshader": False,
        "dawn_enable_vulkan": False,
        "dawn_use_swiftshader": False,
        # No host audio stacks; CharmOS has its own future audio backend.
        "use_alsa": False,
        "use_pulseaudio": False,
        # WebNN's TFLite/LiteRT backends are unused by the browser UI.
        "webnn_use_tflite": False,
        "webnn_use_litert": False,
        # Needed by content/browser media capture paths used by the shell.
        "enable_gpu_channel_media_capture": True,
    })
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
if [[ "$mode" == --ozone-graph ]]; then
  python3 "$root/tools/chromium/audit-cpp-graph.py" "$source" "$output" ozone_runtime
  exit 0
fi
if [[ "$mode" == --v8-graph ]]; then
  python3 "$root/tools/chromium/audit-cpp-graph.py" "$source" "$output" v8_runtime
  exit 0
fi
if [[ "$mode" == --blink-graph ]]; then
  python3 "$root/tools/chromium/audit-cpp-graph.py" "$source" "$output" blink_runtime
  exit 0
fi
if [[ "$mode" == --blink ]]; then
  # Compile the C++/libxml2 Blink libraries; content shell is a later target.
  ninja -C "$output" -k 0 -j"${CHARMOS_BUILD_JOBS:-4}" blink_port
  exit 0
fi
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
    "args": (source / "out/charmos-native/args.gn").read_text(),
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
  ninja -C "$output" -k 0 -j 4 graphics_runtime canvas_runtime ozone_runtime
  for program in graphics_runtime canvas_runtime ozone_runtime; do
    "$compiler/llvm-objcopy" --strip-debug "$output/$program.elf" "$root/build/user/user/$program.elf"
    python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/$program.elf"
  done
fi
if [[ "$mode" == --v8 ]]; then
  ninja -C "$output" -k 0 -j 4 v8_runtime
  # Not named *.elf: tools/run.sh would pack the 60 MB binary into the initrd.
  "$compiler/llvm-objcopy" --strip-debug "$output/v8_runtime.elf" "$root/build/user/user/v8_runtime.bin"
  python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/v8_runtime.bin"
fi
if [[ "$mode" == --blink ]]; then
  # Compile the C++/libxml2 Blink libraries; a runnable content shell is a
  # separate target and is not linked yet.
  ninja -C "$output" -k 0 -j"${CHARMOS_BUILD_JOBS:-4}" blink_port
fi
