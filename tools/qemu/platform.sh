#!/usr/bin/env bash
set -euo pipefail
build="$1"
[[ -f "$build/user/user/platform_smoke.elf" ]] || { echo 'Run tools/chromium/build-platform.sh first' >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
bash "$root/tools/qemu/libc.sh" "$build"
