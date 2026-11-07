#!/usr/bin/env bash
set -euo pipefail
build="$1"
work="$build/network-review-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service compatibility /bin/network_compatibility
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for program in init logd network_addresses network_compatibility sh; do
  puts+=(--put "2:/bin/$program=$build/user/user/$program.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-network-review.img" 96 --esp 40 --root ext2 "${puts[@]}"
