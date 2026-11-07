#!/usr/bin/env bash
# Rebuild + package + guest-test a native target without re-running prepare/GN.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json; print(json.load(open("'"$root"'/tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$cache/release/chromium-$version"
output="$source/out/charmos-native"
target="${1:?usage: rebuild-test.sh TARGET SCENARIO}"
scenario="${2:?usage: rebuild-test.sh TARGET SCENARIO}"
ninja -C "$output" -k 0 -j4 "$target"
"$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
  "$output/$target.elf" "$root/build/user/user/$target.elf"
python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/$target.elf"
cp "$source/third_party/icu/common/icudtl.dat" "$root/build/user/user/icudtl.dat"
python3 "$root/tools/chromium/package-certificate-fixtures.py" "$source" "$root/build/user/user"
bash "$root/tools/test.sh" m10 100 "$scenario"
