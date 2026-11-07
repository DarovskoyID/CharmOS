#!/usr/bin/env bash
# Run through with-cache.sh after the V8 guest gate, with no active compiler.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
case "${1:-}" in ''|--resume) ;; *) echo 'usage: verify-link-dependencies.sh [--resume]' >&2; exit 2 ;; esac
version="$(python3 -c 'import json; print(json.load(open("tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$root/build/chromium/linux-cache/release/chromium-$version"
output="$source/out/charmos-native"
before="$(sha256sum "$output/v8_runtime.elf" | cut -d' ' -f1)"
python3 tools/chromium/prepare-platform.py
if [[ "${1:-}" != --resume ]] || ! python3 tools/chromium/graph_cache.py \
    --is-current "$output" v8_runtime-cpp-graph-report.json; then
  "$source/buildtools/linux64/gn" gen "$output" --root="$source" \
    --fail-on-unused-args --root-target=//build/charmos:base_port \
    > build/chromium/link-dependencies-gn.log 2>&1
fi
ninja -C "$output" -j"${CHARMOS_BUILD_JOBS:-4}" v8_runtime \
  > build/chromium/link-dependencies-build.log 2>&1
after="$(sha256sum "$output/v8_runtime.elf" | cut -d' ' -f1)"
if [[ "$before" != "$after" ]]; then
  bash tools/chromium/package-v8.sh 300 > build/chromium/link-dependencies-guest.log 2>&1
fi
python3 tools/chromium/test-link-dependencies.py "$output"
python3 tools/chromium/audit-cpp-graph.py "$source" "$output" v8_runtime
echo 'Native link dependencies: guest-verified ELF'
