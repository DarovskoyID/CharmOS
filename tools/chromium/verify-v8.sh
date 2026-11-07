#!/usr/bin/env bash
# Native V8 gate: Rust-free graph, build, ELF audit and guest JavaScript test.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
mkdir -p build/chromium
case "${1:-}" in ''|--resume) ;; *) echo 'usage: verify-v8.sh [--resume]' >&2; exit 2 ;; esac
ninja -C build > build/chromium/v8-os-build.log 2>&1
version="$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$root/build/chromium/linux-cache/release/chromium-$version"
output="$source/out/charmos-native"
if [[ "${1:-}" == --resume ]]; then
  python3 tools/chromium/prepare-platform.py
  if python3 tools/chromium/graph_cache.py --is-current "$output" v8_runtime-cpp-graph-report.json; then
    python3 tools/chromium/audit-cpp-graph.py "$source" "$output" v8_runtime
  else
    bash tools/chromium/build-platform.sh --v8-graph > build/chromium/v8-graph.log 2>&1
  fi
else
  bash tools/chromium/build-platform.sh --v8-graph > build/chromium/v8-graph.log 2>&1
fi
ninja -C "$output" -k 0 -j"${CHARMOS_BUILD_JOBS:-4}" v8_runtime > build/chromium/v8-build.log 2>&1
# Keep the 60 MiB binary out of tools/run.sh's *.elf initrd (TCG boot would crawl).
rm -f build/user/user/v8_runtime.elf
"$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
  "$output/v8_runtime.elf" build/user/user/v8_runtime.bin
python3 tools/chromium/audit-elf.py build/user/user/v8_runtime.bin
cp "$source/third_party/icu/common/icudtl.dat" build/user/user/icudtl.dat
python3 tools/chromium/package-certificate-fixtures.py "$source" build/user/user
# TCG boot with the ext2 image can exceed 100 s. The scenario exits via ACPI
# poweroff as soon as the clean-shutdown line appears.
bash tools/test.sh m10 300 v8_runtime
