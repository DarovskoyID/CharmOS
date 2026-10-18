#!/usr/bin/env bash
set -euo pipefail
build="$1"
[[ -f "$build/user/user/runtime_cpp.elf" ]] || { echo 'Build the native C++ runtime first' >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
bash "$root/tools/qemu/libc.sh" "$build"
