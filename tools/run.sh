#!/usr/bin/env bash
# Запуск CharmOS в QEMU (UEFI/OVMF). Serial — в stdout.
#   tools/run.sh [--gdb] [--trace] [--headless] [--test] [--config <имя>] [--cmdline <строка>]
#   --gdb       gdb-стаб на :1234, CPU стоит до подключения (-s -S)
#   --trace     -d int,guest_errors,cpu_reset (медленно и шумно под OVMF)
#   --headless  без окна
#   --test      режим тестов: без -no-shutdown (иначе isa-debug-exit в QEMU ≥ 9
#               ставит VM на паузу вместо выхода)
#   --config    доп. устройства из tools/qemu/<имя>.args
#   --cmdline   строка для ESP:/charmos/cmdline.txt (без опции файла нет)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${CHARMOS_BUILD:-$root/build}"
esp="$build/esp"

gdb=0; trace=0; headless=0; testmode=0; config=""; cmdline=""; has_cmdline=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --gdb) gdb=1 ;;
    --trace) trace=1 ;;
    --headless) headless=1 ;;
    --test) testmode=1; headless=1 ;;
    --config) config="$2"; shift ;;
    --cmdline) cmdline="$2"; has_cmdline=1; shift ;;
    *) echo "run.sh: unknown option $1" >&2; exit 2 ;;
  esac
  shift
done

find_fw() {
  for f in "$@"; do [[ -f "$f" ]] && { echo "$f"; return; }; done
  echo "run.sh: OVMF not found (sudo apt install ovmf)" >&2; exit 2
}
ovmf_code=$(find_fw /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd)
ovmf_vars=$(find_fw /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd)

# ESP собирается из каталога: QEMU отдаёт его как FAT-диск (vvfat).
mkdir -p "$esp/EFI/BOOT"
cp "$build/uefi/boot/uefi/BOOTX64.efi" "$esp/EFI/BOOT/BOOTX64.EFI"
mkdir -p "$esp/charmos"
cp "$build/kernel/kernel/kernel.elf" "$esp/charmos/kernel.elf"
rm -f "$esp/charmos/cmdline.txt"
if [[ $has_cmdline -eq 1 ]]; then printf '%s\n' "$cmdline" >"$esp/charmos/cmdline.txt"; fi

# initrd (docs/abi/initrd.md): все собранные программы пользователя как bin/<имя>.
mkinitrd="$build/host/tools/mkinitrd/mkinitrd"
rm -f "$esp/charmos/initrd"
if [[ -x "$mkinitrd" ]] && compgen -G "$build/user/user/*.elf" >/dev/null; then
  initrd_args=()
  for f in "$build"/user/user/*.elf; do initrd_args+=("bin/$(basename "$f" .elf)=$f"); done
  for f in "$build"/user/drivers/*.elf "$build"/user/drivers/*.toml; do
    [[ -f "$f" ]] && initrd_args+=("drivers/$(basename "$f")=$f")
  done
  "$mkinitrd" "$esp/charmos/initrd" "${initrd_args[@]}"
fi
cp "$ovmf_vars" "$build/ovmf_vars.fd"

args=(
  -machine q35 -cpu max -m 512M -smp 4
  -drive if=pflash,format=raw,readonly=on,file="$ovmf_code"
  -drive if=pflash,format=raw,file="$build/ovmf_vars.fd"
  -drive format=raw,file=fat:rw:"$esp"
  -serial stdio
  -device isa-debug-exit,iobase=0xf4,iosize=0x04
  -no-reboot
  -D "$build/qemu.log"
)
if [[ $trace -eq 1 ]]; then args+=(-d int,guest_errors,cpu_reset); else args+=(-d guest_errors,cpu_reset); fi
[[ $testmode -eq 0 ]] && args+=(-no-shutdown)
[[ $gdb -eq 1 ]] && args+=(-s -S)
[[ $headless -eq 1 ]] && args+=(-display none)
if [[ -n "$config" ]]; then
  # Подготовка сценария (образы дисков и т. п.): tools/qemu/<имя>.sh <каталог сборки>.
  if [[ -f "$root/tools/qemu/$config.sh" ]] && ! bash "$root/tools/qemu/$config.sh" "$build"; then
    echo "run.sh: tools/qemu/$config.sh failed" >&2
    exit 3
  fi
  mapfile -t extra < <(grep -v '^\s*#' "$root/tools/qemu/$config.args" | tr ' ' '\n' | grep -v '^$')
  args+=("${extra[@]}")
fi

exec qemu-system-x86_64 "${args[@]}"
