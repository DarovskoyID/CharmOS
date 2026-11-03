#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
python3 tools/chromium/prepare-platform.py
source="$root/build/chromium/linux-cache/release/chromium-$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
output="$source/out/charmos-platform"
"$source/buildtools/linux64/gn" gen "$output" --root="$source" --fail-on-unused-args --root-target=//build/charmos:base_port
python3 tools/chromium/audit-cpp-graph.py "$source" "$output" canvas_runtime
ninja -C "$output" -j4 canvas_runtime > build/chromium/canvas-build.log 2>&1
"$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
  "$output/canvas_runtime.elf" build/user/user/canvas_runtime.elf
python3 tools/chromium/audit-elf.py build/user/user/canvas_runtime.elf
bash tools/test.sh m10 100 canvas_runtime > build/chromium/canvas-guest.log 2>&1 || {
  tail -20 build/chromium/canvas-guest.log; exit 1;
}
tail -1 build/chromium/canvas-guest.log
