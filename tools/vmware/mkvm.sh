#!/usr/bin/env bash
# Машина CharmOS для VMware Workstation из build/charmos.img (tools/vbox/mkimage.sh):
# build/vmware/CharmOS.vmx + charmos.vmdk. EFI, AHCI, e1000, виртуальный VT-d
# (драйверам нужен DMA через IOMMU), COM1 — в файл build/vmware/serial.log.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/vmware"
mkdir -p "$out"
qemu-img convert -f raw -O vmdk -o adapter_type=lsilogic,subformat=monolithicSparse \
  "$root/build/charmos.img" "$out/charmos.vmdk"
cat >"$out/CharmOS.vmx" <<'EOF'
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "19"
displayName = "CharmOS"
guestOS = "other-64"
firmware = "efi"
memsize = "1024"
numvcpus = "2"
vvtd.enable = "TRUE"
sata0.present = "TRUE"
sata0:0.present = "TRUE"
sata0:0.fileName = "charmos.vmdk"
ethernet0.present = "TRUE"
ethernet0.virtualDev = "e1000"
ethernet0.connectionType = "nat"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "serial.log"
serial0.yieldOnMsrRead = "TRUE"
usb.present = "FALSE"
sound.present = "FALSE"
floppy0.present = "FALSE"
EOF
echo "VM: $out/CharmOS.vmx"
