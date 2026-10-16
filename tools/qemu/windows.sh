# Системный диск: init запускает logd, wm, два клиента окон и шелл.
build="$1"
work="$build/windows-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service amber /bin/wdemo amber f5c518
service ember /bin/wdemo ember ff8a00 280 180 1000
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for p in init logd sh wm wdemo files; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-windows.img" 96 --esp 40 --root ext2 "${puts[@]}"
