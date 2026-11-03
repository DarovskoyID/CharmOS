#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
mountpoint -q build/chromium/linux-cache || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
native_source="build/chromium/linux-cache/release/chromium-$version"
output="$native_source/out/charmos-platform"
ninja -C "$output" -j4 graphics_runtime full_runtime base_runtime > build/chromium/graphics-relink.log 2>&1
for item in graphics_runtime:graphics_runtime full_runtime:runtime_cpp base_runtime:base_runtime; do
  from_name=${item%:*}
  to_name=${item#*:}
  "$native_source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
    "$output/$from_name.elf" "build/user/user/$to_name.elf"
  python3 tools/chromium/audit-elf.py "build/user/user/$to_name.elf"
done
for item in m10:window_pixels m10:graphics_runtime browser:runtime_cpp browser:base_runtime m10:windows; do
  phase=${item%:*}
  scenario=${item#*:}
  log="build/chromium/graphics-guest-$scenario.log"
  if ! bash tools/test.sh "$phase" 150 "$scenario" > "$log" 2>&1; then
    tail -12 "$log"
    exit 1
  fi
  tail -1 "$log"
done
