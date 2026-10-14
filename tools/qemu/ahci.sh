# Образ для AHCI: GPT, ESP, корень ext2 с /etc/hello.txt.
set -e
build="$1"
work="$build/fs-work"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-ahci.img" 96 --esp 40 --root ext2 \
  --put "2:/etc/hello.txt=$work/hello"
