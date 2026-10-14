# После nvme3_crash: образ проходит e2fsck (сторонний инструмент только на хосте).
set -e
build="$1"
start=$((2048 + 40 * 2048))
sectors=$(( $(stat -c %s "$build/disk-nvme.img") / 512 - 34 - start ))
dd if="$build/disk-nvme.img" of="$build/fs-work/nvme-root.img" bs=512 skip="$start" count="$sectors" status=none
e2fsck=$(command -v e2fsck || echo /sbin/e2fsck)
"$e2fsck" -fn "$build/fs-work/nvme-root.img" >&2
