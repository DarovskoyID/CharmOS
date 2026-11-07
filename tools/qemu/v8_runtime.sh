#!/usr/bin/env bash
set -euo pipefail
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
work="$build/v8-runtime-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
if [[ -f "$build/user/user/icudtl.dat" ]]; then
  puts+=(--put "2:/bin/icudtl.dat=$build/user/user/icudtl.dat")
fi
for program in init logd sh; do
  puts+=(--put "2:/bin/$program=$build/user/user/$program.elf")
done
# v8_runtime is deliberately not named *.elf: tools/run.sh packs every
# build/user/user/*.elf into the initrd, and a 60 MB V8 binary makes OVMF/FAT
# boot take minutes under TCG. The guest loads it from the ext2 root instead.
v8="$build/user/user/v8_runtime.bin"
[[ -f "$v8" ]] || v8="$build/user/user/v8_runtime.elf"
puts+=(--put "2:/bin/v8_runtime=$v8")
"$build/host/tools/mkdisk/mkdisk" "$build/disk-v8-runtime.img" 128 --esp 40 --root ext2 "${puts[@]}"
