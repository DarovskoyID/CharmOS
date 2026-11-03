#!/usr/bin/env bash
set -euo pipefail
build="$1"
[[ -f "$build/user/user/base_runtime.elf" ]] || { echo 'Build the native base runtime first' >&2; exit 2; }
"$build/host/tools/mkdisk/mkdisk" "$build/disk-spawn.img" 96 --esp 40 --root ext2 \
  --put "2:/bin/base_runtime=$build/user/user/base_runtime.elf"
