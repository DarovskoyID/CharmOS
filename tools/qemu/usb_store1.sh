# USB flash image: root ext2 exercised by the existing fs_test program.
set -e
build="$1"
work="$build/usb-store-work"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-usb-store.img" 96 --esp 40 --root ext2 \
  --put "2:/etc/hello.txt=$work/hello" --put "1:/EFI/readme.txt=$work/hello"
