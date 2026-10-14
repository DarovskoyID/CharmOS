# Образ для сценариев fs1/fs2: GPT, ESP (FAT32) и корень ext2 с /etc/hello.txt.
build="$1"
work="$build/fs-work"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-fs.img" 96 --esp 40 --root ext2 \
  --put "2:/etc/hello.txt=$work/hello" --put "1:/EFI/readme.txt=$work/hello"
