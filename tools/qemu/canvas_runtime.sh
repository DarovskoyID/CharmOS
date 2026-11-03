#!/usr/bin/env bash
set -euo pipefail
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
work="$build/canvas-runtime-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service canvas /bin/canvas_runtime
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for program in init logd wm canvas_runtime sh; do
  puts+=(--put "2:/bin/$program=$build/user/user/$program.elf")
done
for notice in "$root"/tools/chromium/licenses/*; do
  [[ -f "$notice" ]] && puts+=(--put "2:/share/licenses/chromium/$(basename "$notice")=$notice")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-canvas-runtime.img" 128 --esp 40 --root ext2 "${puts[@]}"
