source "$(dirname "$0")/windows.sh" "$1"
# A long UTF-8 document exercises wrapped preview pages, not just short init.conf.
for ((i=1; i<=60; ++i)); do
  printf 'Строка %02d: текст для проверки прокрутки и границ предпросмотра.\n' "$i"
done >"$work/z-preview.txt"
puts+=(--put "2:/etc/z-preview.txt=$work/z-preview.txt")
"$build/host/tools/mkdisk/mkdisk" "$build/disk-windows.img" 96 --esp 40 --root ext2 "${puts[@]}"
