#!/usr/bin/env bash
# Reopen the CharmOS content-shell browser in QEMU with KVM.
# Run as root so /dev/kvm and the cache loop mount are available:
#   wsl -d Ubuntu -u root -- bash /mnt/a/CharmOS/tools/qemu/open-browser.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
build="${1:-$root/build}"
export CHARMOS_CACHE_IMAGE="${CHARMOS_CACHE_IMAGE:-/home/ivan/linux-cache.ext4}"

bash "$root/tools/chromium/with-cache.sh" 1000 1000 \
  bash "$root/tools/qemu/browser.sh" "$build"
cp "$root/build/kernel/kernel/kernel.elf" "$build/esp/charmos/kernel.elf"
rm -f /tmp/charmos-mon

exec env DISPLAY="${DISPLAY:-:0}" qemu-system-x86_64 \
  -machine q35 -cpu max -m 2048M -smp 4 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file="$build/ovmf_vars.fd" \
  -drive format=raw,file=fat:rw:"$build/esp" \
  -serial file:"$build/browser-serial.log" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 -no-reboot \
  -D "$build/qemu.log" -accel kvm -d guest_errors,cpu_reset -no-shutdown \
  -device intel-iommu \
  -drive if=none,id=d0,format=raw,file="$build/disk-browser.img" \
  -device virtio-blk-pci,drive=d0,disable-legacy=on,iommu_platform=on \
  -nic none \
  -monitor unix:/tmp/charmos-mon,server,nowait -display gtk
