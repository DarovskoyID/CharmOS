#!/usr/bin/env bash
set -euo pipefail
build="$1"
[[ -f "$build/user/user/runtime_cpp.elf" ]] || { echo 'Build the native C++ runtime first' >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$build/spawn-work"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-spawn.img" 96 --esp 40 --root ext2 \
  --put "2:/etc/hello.txt=$work/hello" \
  --put "2:/bin/spawn_child=$build/user/user/spawn_child.elf" \
  --put "2:/bin/runtime_cpp=$build/user/user/runtime_cpp.elf"
