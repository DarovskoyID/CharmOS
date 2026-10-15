#!/usr/bin/env bash
# Загрузочный образ CharmOS для VirtualBox (и любой машины с UEFI):
# GPT — ESP (загрузчик, ядро, initrd, cmdline), корень ext2 (программы, init.conf,
# сайт httpd), закрытая область (раздел 3). Результат: build/charmos.img.
# Затем tools/vbox/vm.ps1 (Windows) делает из него VDI и машину.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build"
work="$build/vbox-work"
rm -rf "$work"
mkdir -p "$work"

# initrd (docs/abi/initrd.md): программы и драйверы, как в tools/run.sh.
args=()
for f in "$build"/user/user/*.elf; do args+=("bin/$(basename "$f" .elf)=$f"); done
for f in "$build"/user/drivers/*.elf "$build"/user/drivers/*.toml; do
  [[ -f "$f" ]] && args+=("drivers/$(basename "$f")=$f")
done
"$build/host/tools/mkinitrd/mkinitrd" "$work/initrd" "${args[@]}"
printf '%s\n' "${CHARMOS_CMDLINE:-init=bin/init root=disk0p2}" >"$work/cmdline.txt"

echo "hello from CharmOS" >"$work/hello"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service pkgd /bin/pkgd
service httpd /bin/httpd 8080 /srv/www
console sh /bin/sh
EOF
cat >"$work/index.html" <<'EOF'
<!doctype html>
<html><head><title>CharmOS</title></head>
<body><h1>Hello from CharmOS</h1><p>Served by a user-space HTTP server from an ext2 disk.</p></body></html>
EOF

puts=(--put "1:/EFI/BOOT/BOOTX64.EFI=$build/uefi/boot/uefi/BOOTX64.efi"
      --put "1:/charmos/kernel.elf=$build/kernel/kernel/kernel.elf"
      --put "1:/charmos/initrd=$work/initrd"
      --put "1:/charmos/cmdline.txt=$work/cmdline.txt"
      --put "2:/etc/hello.txt=$work/hello" --put "2:/etc/init.conf=$work/init.conf"
      --put "2:/srv/www/index.html=$work/index.html")
for p in init logd sh echo cat ls mkdir rm sleep httpd pkgd corrupt; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/charmos.img" 256 --esp 64 --system 64 --root ext2 "${puts[@]}"
echo "image: $build/charmos.img"
