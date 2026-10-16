set -e
build="$1"
work="$build/usb-combo-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service amber /bin/wdemo amber f5c518
service ember /bin/wdemo ember ff8a00 280 180 1000 io
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for p in init logd sh wm wdemo; do puts+=(--put "2:/bin/$p=$build/user/user/$p.elf"); done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-usb-combo.img" 96 --esp 40 --root ext2 "${puts[@]}"
