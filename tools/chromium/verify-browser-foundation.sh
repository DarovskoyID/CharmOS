#!/usr/bin/env bash
# One native profile and one compile for the completed Ozone/Mojo/libc package.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
mkdir -p build/chromium
case "${1:-}" in ''|--resume) ;; *) echo 'usage: verify-browser-foundation.sh [--resume]' >&2; exit 2 ;; esac
ninja -C build > build/chromium/foundation-os-build.log 2>&1
source="$root/build/chromium/linux-cache/release/chromium-$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
output="$source/out/charmos-native"
if [[ "${1:-}" == --resume ]]; then
  python3 tools/chromium/prepare-platform.py
  if python3 tools/chromium/graph_cache.py --is-current "$output" \
      ozone_runtime-cpp-graph-report.json mojo_runtime-cpp-graph-report.json; then
    python3 tools/chromium/audit-cpp-graph.py "$source" "$output" ozone_runtime
  else
    # GN inputs or manifests changed, or there is no verified receipt yet.
    bash tools/chromium/build-platform.sh --ozone-graph
  fi
else
  bash tools/chromium/build-platform.sh --ozone-graph
fi
python3 tools/chromium/audit-cpp-graph.py "$source" "$output" mojo_runtime
ninja -C "$output" -k 0 -j4 ozone_runtime mojo_runtime > build/chromium/foundation-build.log 2>&1
for target in ozone_runtime mojo_runtime; do
  "$source/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy" --strip-debug \
    "$output/$target.elf" "build/user/user/$target.elf"
  python3 tools/chromium/audit-elf.py "build/user/user/$target.elf"
done
cp "$source/third_party/icu/common/icudtl.dat" build/user/user/icudtl.dat
python3 tools/chromium/package-certificate-fixtures.py "$source" build/user/user
for scenario in network_review ozone_runtime mojo_runtime; do
  log="build/chromium/foundation-$scenario-guest.log"
  if ! bash tools/test.sh m10 100 "$scenario" > "$log" 2>&1; then
    tail -20 "$log"
    exit 1
  fi
  tail -1 "$log"
done
