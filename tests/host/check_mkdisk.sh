#!/usr/bin/env bash
# Перекрёстная проверка: образ, собранный tools/mkdisk, проходит e2fsck
# (сторонний инструмент только на хосте, в ОС не попадает). Без e2fsck — пропуск.
set -euo pipefail
mkdisk="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
command -v e2fsck >/dev/null || [[ -x /sbin/e2fsck ]] || { echo "e2fsck not found, skipped"; exit 0; }
e2fsck=$(command -v e2fsck || echo /sbin/e2fsck)
head -c 200000 /dev/urandom >"$work/blob"
echo "hello from mkdisk" >"$work/hello"
for block in 1024 4096; do
  "$mkdisk" "$work/disk.img" 128 --esp 40 --root ext2 --block "$block" \
    --put "2:/etc/hello.txt=$work/hello" --put "2:/data/deep/dir/blob=$work/blob" \
    --put "1:/EFI/BOOT/readme.txt=$work/hello"
  # Раздел 2 начинается сразу за ESP: 2048 + 40 МиБ, до конца минус 34 сектора.
  start=$((2048 + 40 * 2048))
  sectors=$(( $(stat -c %s "$work/disk.img") / 512 - 34 - start ))
  dd if="$work/disk.img" of="$work/root.img" bs=512 skip="$start" count="$sectors" status=none
  "$e2fsck" -fn "$work/root.img"
  debugfs -R "cat /data/deep/dir/blob" "$work/root.img" 2>/dev/null | cmp - "$work/blob"
done
# FAT32: ESP и корень FAT32 проходят fsck.fat (dosfstools), если он есть.
fsckfat=$(command -v fsck.fat || { [[ -x /usr/sbin/fsck.fat ]] && echo /usr/sbin/fsck.fat; } || true)
if [[ -n "$fsckfat" ]]; then
  "$mkdisk" "$work/disk.img" 128 --esp 40 --root fat32     --put "2:/etc/hello.txt=$work/hello" --put "2:/A Long File Name With Spaces.data=$work/blob"     --put "1:/EFI/BOOT/readme.txt=$work/hello"
  dd if="$work/disk.img" of="$work/esp.img" bs=512 skip=2048 count=$((40 * 2048)) status=none
  start=$((2048 + 40 * 2048))
  sectors=$(( $(stat -c %s "$work/disk.img") / 512 - 34 - start ))
  dd if="$work/disk.img" of="$work/root.img" bs=512 skip="$start" count="$sectors" status=none
  "$fsckfat" -n "$work/esp.img"
  "$fsckfat" -n "$work/root.img"
else
  echo "fsck.fat not found, FAT check skipped"
fi
echo "mkdisk images pass e2fsck and fsck.fat"
