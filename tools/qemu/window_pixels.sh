#!/usr/bin/env bash
set -euo pipefail
build="$1"
work="$build/window-pixels-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service pixels /bin/window_pixels
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for program in init logd wm window_pixels sh; do
  puts+=(--put "2:/bin/$program=$build/user/user/$program.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-window-pixels.img" 96 --esp 40 --root ext2 "${puts[@]}"
