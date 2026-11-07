#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json; print(json.load(open("'"$root"'/tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$cache/release/chromium-$version"
for f in "$@"; do
  orig="$cache/platform-originals/$f"
  if [ -f "$orig" ]; then cp "$orig" "$source/$f"; echo "restored $f"; else echo "no backup: $f"; fi
done
