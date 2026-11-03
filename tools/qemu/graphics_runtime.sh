#!/usr/bin/env bash
set -euo pipefail
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
work="$build/graphics-runtime-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service graphics /bin/graphics_runtime --test
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf"
      --put "2:/fonts/ui.ttf=$root/assets/fonts/space-grotesk/SpaceGrotesk-Medium.ttf"
      --put "2:/fonts/ui-OFL.txt=$root/assets/fonts/space-grotesk/OFL.txt"
      --put "2:/fonts/mono.ttf=$root/assets/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf"
      --put "2:/fonts/mono-OFL.txt=$root/assets/fonts/jetbrains-mono/OFL.txt")
for program in init logd wm graphics_runtime sh; do
  puts+=(--put "2:/bin/$program=$build/user/user/$program.elf")
done
for notice in "$root"/tools/chromium/licenses/*; do
  [[ -f "$notice" ]] && puts+=(--put "2:/share/licenses/chromium/$(basename "$notice")=$notice")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-graphics-runtime.img" 128 --esp 40 --root ext2 "${puts[@]}"
