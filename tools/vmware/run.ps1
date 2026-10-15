# Пересобрать образ, машину VMware и загрузить её без окна; через Wait секунд
# показать serial-журнал (build/vmware/serial.log) и выключить машину.
param([int]$Wait = 60, [switch]$Keep,
      [string]$VmRun = "B:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe")
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vmx = Join-Path $root "build\vmware\CharmOS.vmx"
$log = Join-Path $root "build\vmware\serial.log"
& $VmRun -T ws stop $vmx hard 2>$null | Out-Null
wsl -d Ubuntu --cd /mnt/a/CharmOS -- bash -c "ninja -C build >/dev/null && bash tools/vbox/mkimage.sh >/dev/null && bash tools/vmware/mkvm.sh >/dev/null"
Remove-Item $log -ErrorAction SilentlyContinue
& $VmRun -T ws start $vmx nogui
Start-Sleep -Seconds $Wait
if (Test-Path $log) { Get-Content $log } else { "no serial log" }
if (-not $Keep) { & $VmRun -T ws stop $vmx hard 2>$null | Out-Null }
