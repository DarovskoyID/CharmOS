source "$(dirname "$0")/desktop.sh" "$1"
printf 'Привет, CharmOS!\nПроверка русского имени и пути.\n' >"$work/russian.txt"
puts+=(--put "2:/Данные/заметка.txt=$work/russian.txt")
"$build/host/tools/mkdisk/mkdisk" "$build/disk-desktop.img" 96 --esp 40 --root ext2 "${puts[@]}"
