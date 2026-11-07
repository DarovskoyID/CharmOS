#!/usr/bin/env bash
set -euo pipefail
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
work="$build/ozone-runtime-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service ozone /bin/ozone_runtime
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf"
      --put "2:/bin/icudtl.dat=$build/user/user/icudtl.dat")
for fixture in net-test-ok.pem net-test-root.pem net-test-expired.pem; do
  puts+=(--put "2:/bin/$fixture=$build/user/user/$fixture")
done
for program in init logd wm ozone_runtime sh; do
  puts+=(--put "2:/bin/$program=$build/user/user/$program.elf")
done
for notice in "$root"/tools/chromium/licenses/*; do
  [[ -f "$notice" ]] && puts+=(--put "2:/share/licenses/chromium/$(basename "$notice")=$notice")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-ozone-runtime.img" 128 --esp 40 --root ext2 "${puts[@]}"
