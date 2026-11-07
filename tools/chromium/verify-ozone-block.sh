#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
bash tools/chromium/build-platform.sh --ozone-graph
source="$root/build/chromium/linux-cache/release/chromium-$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
output="$source/out/charmos-native"
ninja -C "$output" -k 0 -j4 ozone_runtime > build/chromium/ozone-build.log 2>&1
"$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
  "$output/ozone_runtime.elf" build/user/user/ozone_runtime.elf
python3 tools/chromium/audit-elf.py build/user/user/ozone_runtime.elf
cp "$source/third_party/icu/common/icudtl.dat" build/user/user/icudtl.dat
python3 tools/chromium/package-certificate-fixtures.py "$source" build/user/user
bash tools/test.sh m10 100 ozone_runtime > build/chromium/ozone-guest.log 2>&1 || {
  tail -20 build/chromium/ozone-guest.log; exit 1;
}
tail -1 build/chromium/ozone-guest.log
