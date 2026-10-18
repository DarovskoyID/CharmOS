#!/usr/bin/env bash
set -euo pipefail
build="$1"
[[ -f "$build/user/user/runtime_json.elf" ]] || { echo 'Build the native C++ JSON runtime first' >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
bash "$root/tools/qemu/libc.sh" "$build"
