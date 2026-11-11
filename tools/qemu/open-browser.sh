#!/usr/bin/env bash
# Reopen the CharmOS content-shell browser in QEMU with KVM.
# Run as root so /dev/kvm and the cache loop mount are available:
#   wsl -d Ubuntu -u root -- bash /mnt/a/CharmOS/tools/qemu/open-browser.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
export CHARMOS_CACHE_IMAGE="${CHARMOS_CACHE_IMAGE:-/home/ivan/linux-cache.ext4}"
export DISPLAY="${DISPLAY:-:0}"
# run.sh builds the disk image, the native ESP (kernel + initrd) and starts
# QEMU with the browser config, so this wrapper cannot drift from it. The
# cache must be mounted around the whole run because browser.sh reads the
# freshly built content_shell from it.
exec bash "$root/tools/chromium/with-cache.sh" 0 0 \
  bash "$root/tools/run.sh" --config browser --accel kvm \
  --cmdline 'init=bin/init root=disk0p2'
