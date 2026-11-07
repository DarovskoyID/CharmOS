#!/usr/bin/env bash
# Package an already-built v8_runtime.elf and run the guest scenario.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json; print(json.load(open("'"$root"'/tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$cache/release/chromium-$version"
output="$source/out/charmos-native"
timeout="${1:-400}"
# Keep the binary out of tools/run.sh's initrd glob (*.elf): the guest reads it
# from the ext2 root, and a 60 MB initrd entry makes TCG boot take minutes.
rm -f "$root/build/user/user/v8_runtime.elf"
"$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
  "$output/v8_runtime.elf" "$root/build/user/user/v8_runtime.bin"
python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/v8_runtime.bin"
cp "$source/third_party/icu/common/icudtl.dat" "$root/build/user/user/icudtl.dat"
python3 "$root/tools/chromium/package-certificate-fixtures.py" "$source" "$root/build/user/user"
bash "$root/tools/test.sh" m10 "$timeout" v8_runtime
