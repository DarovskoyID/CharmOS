set -e
build="$1"
work="$build/hda-work"
mkdir -p "$work"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service audio /bin/audio_test
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf")
for p in init logd sh audio_test; do puts+=(--put "2:/bin/$p=$build/user/user/$p.elf"); done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-hda.img" 96 --esp 40 --root ext2 "${puts[@]}"
