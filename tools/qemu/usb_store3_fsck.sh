set -e
build="$1"
start=$((2048 + 40 * 2048))
sectors=$(( $(stat -c %s "$build/disk-usb-store.img") / 512 - 34 - start ))
dd if="$build/disk-usb-store.img" of="$build/usb-store-work/root-check.img" bs=512 skip="$start" count="$sectors" status=none
e2fsck=$(command -v e2fsck || echo /sbin/e2fsck)
"$e2fsck" -fn "$build/usb-store-work/root-check.img" >&2
