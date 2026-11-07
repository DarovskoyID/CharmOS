#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
# Keep the same native profile as Ozone so switching probes does not toggle
# build flags or invalidate the already compiled graphics/network objects.
bash tools/chromium/build-platform.sh --ozone-graph
source="$root/build/chromium/linux-cache/release/chromium-$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
output="$source/out/charmos-native"
python3 tools/chromium/audit-cpp-graph.py "$source" "$output" mojo_runtime
ninja -C "$output" -k 0 -j4 mojo_runtime > build/chromium/mojo-build.log 2>&1
"$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
  "$output/mojo_runtime.elf" build/user/user/mojo_runtime.elf
python3 tools/chromium/audit-elf.py build/user/user/mojo_runtime.elf
bash tools/test.sh m10 90 mojo_runtime > build/chromium/mojo-guest.log 2>&1 || {
  tail -20 build/chromium/mojo-guest.log; exit 1;
}
tail -1 build/chromium/mojo-guest.log
