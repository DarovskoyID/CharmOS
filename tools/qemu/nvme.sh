# Сначала: образ FAT32 после сценария fs3_fat (писала CharmOS) проходит
# fsck.fat. Затем — образ для NVMe: GPT, ESP, корень ext2 с /etc/hello.txt.
set -e
build="$1"
work="$build/fs-work"
mkdir -p "$work"
if [[ -f "$build/disk-fat.img" ]]; then
  start=$((2048 + 40 * 2048))
  sectors=$(( $(stat -c %s "$build/disk-fat.img") / 512 - 34 - start ))
  dd if="$build/disk-fat.img" of="$work/fat-root.img" bs=512 skip="$start" count="$sectors" status=none
  fsckfat=$(command -v fsck.fat || echo /usr/sbin/fsck.fat)
  "$fsckfat" -n "$work/fat-root.img" >&2
fi
echo "hello from mkdisk" >"$work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-nvme.img" 96 --esp 40 --root ext2 --block 4096 \
  --put "2:/etc/hello.txt=$work/hello"
