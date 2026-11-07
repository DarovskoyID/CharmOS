#!/usr/bin/env bash
# Build one or more native GN targets against the prepared Chromium cache.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json; print(json.load(open("'"$root"'/tools/chromium/upstream.json"))["chromium"]["version"])')"
source="$cache/release/chromium-$version"
ninja -C "$source/out/charmos-native" -k 0 -j"${CHARMOS_BUILD_JOBS:-4}" "$@"
