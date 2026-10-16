# Системный диск с компоновщиком: init запускает logd, wm и шелл.
build="$1"
work="$build/gui-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for p in init logd sh wm; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-gui.img" 96 --esp 40 --root ext2 "${puts[@]}"
