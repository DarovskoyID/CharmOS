set -eu
build="$1"
work="$build/desktop-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for p in init logd sh wm files calc edit wdemo cat ls echo mkdir rm sleep; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-desktop.img" 96 --esp 40 --root ext2 "${puts[@]}"
