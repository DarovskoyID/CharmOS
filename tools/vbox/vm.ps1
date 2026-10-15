# Машина CharmOS в VirtualBox из build/charmos.img (tools/vbox/mkimage.sh).
# EFI, ICH9 + Intel IOMMU (драйверам нужен DMA через IOMMU), AHCI, e1000,
# COM1 — в файл build/vbox-serial.log. Повторный запуск пересоздаёт машину.
param([string]$VBox = "B:\virtualbox\VBoxManage.exe", [string]$Name = "CharmOS")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$img = Join-Path $root "build\charmos.img"
$vdi = Join-Path $root "build\charmos.vdi"
$log = Join-Path $root "build\vbox-serial.log"

if ((& $VBox list vms) -match "`"$Name`"") {
  & $VBox controlvm $Name poweroff 2>$null | Out-Null
  Start-Sleep -Seconds 2
  & $VBox unregistervm $Name --delete | Out-Null
}
if (Test-Path $vdi) {
  & $VBox closemedium disk $vdi --delete 2>$null | Out-Null
  if (Test-Path $vdi) { Remove-Item $vdi }
}
& $VBox convertfromraw $img $vdi --format VDI | Out-Null
& $VBox createvm --name $Name --ostype Other_64 --register | Out-Null
& $VBox modifyvm $Name --firmware efi64 --chipset ich9 --iommu intel --memory 1024 --cpus 2 `
  --ioapic on --nic1 nat --nictype1 82540EM --natpf1 "http,tcp,127.0.0.1,18080,,8080" `
  --uart1 0x3F8 4 --uartmode1 file $log --graphicscontroller vboxsvga --audio-enabled off --usb off
& $VBox storagectl $Name --name SATA --add sata --controller IntelAhci --portcount 1
& $VBox storageattach $Name --storagectl SATA --port 0 --device 0 --type hdd --medium $vdi
"VM $Name ready. Start: & '$VBox' startvm $Name ; serial log: $log"
