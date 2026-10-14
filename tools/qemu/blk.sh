# Пустой образ диска 64 МиБ для сценария blk (создаётся заново на каждый запуск).
build="$1"
rm -f "$build/disk-blk.img"
truncate -s 64M "$build/disk-blk.img"
