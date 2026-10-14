# Сначала: образ ext2 после двух загрузок CharmOS проходит e2fsck (сторонний
# инструмент только на хосте). Затем — образ с корнем FAT32.
set -e
build="$1"
start=$((2048 + 40 * 2048))
sectors=$(( $(stat -c %s "$build/disk-fs.img") / 512 - 34 - start ))
dd if="$build/disk-fs.img" of="$build/fs-work/root.img" bs=512 skip="$start" count="$sectors" status=none
e2fsck=$(command -v e2fsck || echo /sbin/e2fsck)
"$e2fsck" -fn "$build/fs-work/root.img" >&2
echo "hello from mkdisk" >"$build/fs-work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-fat.img" 96 --esp 40 --root fat32 \
  --put "2:/etc/hello.txt=$build/fs-work/hello"
