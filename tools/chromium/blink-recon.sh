#!/usr/bin/env bash
# Read-only reconnaissance of the Blink/content dependency closure.
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json; print(json.load(open("'"$root"'/tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$cache/release/chromium-$version"
output="$source/out/charmos-native"
gn="$source/buildtools/linux64/gn"

for label in "//third_party/blink/public:blink" "//content/public/app:app"; do
  echo "=== $label ==="
  if ! "$gn" desc --root="$source" --root-target=//build/charmos:base_port \
      "$output" "$label" deps --all > /tmp/blink_deps.txt 2>/tmp/blink_err.txt; then
    echo "  (not describable): $(head -1 /tmp/blink_err.txt)"
    continue
  fi
  echo "  total=$(wc -l < /tmp/blink_deps.txt)"
  echo "  rust=$(grep -cE '//third_party/rust/|//build/rust:' /tmp/blink_deps.txt || true)"
  grep -E '//third_party/rust/|//build/rust:' /tmp/blink_deps.txt | sort -u | head -20
  echo "  modules:"
  sed -E 's#^//##; s#/.*##' /tmp/blink_deps.txt | sort | uniq -c | sort -rn | head -10
done

echo "=== Rust-removal switches found upstream ==="
grep -rn --include=*.gni 'enable_jxl_decoder' "$source/third_party/blink/renderer/config.gni"
grep -rn --include=*.gni 'v8_enable_temporal_support' "$source/v8/gni/v8.gni" | head -2
echo "  (blink/renderer/core/BUILD.gn unconditionally depends on rust xml via xml_ffi)"
