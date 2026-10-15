#!/usr/bin/env bash
# Образцы ACPI-таблиц для хост-тестов AML (tests/host/data/acpi, в git не
# хранятся: это данные чужих прошивок). QEMU — всегда; VMware — если есть
# машина tools/vmware (нужна Windows и vmrun; см. tools/vmware/run.ps1).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/tests/host/data/acpi"
mkdir -p "$out"
timeout 60 "$root/tools/run.sh" --test --cmdline 'acpi=dump' </dev/null >"$root/build/acpidump-qemu.log" 2>&1 || true
python3 "$root/tools/acpi-extract.py" "$root/build/acpidump-qemu.log" "$out" qemu-q35
if [[ -f "$root/build/vmware/serial.log" ]] && grep -q 'acpidump: begin' "$root/build/vmware/serial.log"; then
  python3 "$root/tools/acpi-extract.py" "$root/build/vmware/serial.log" "$out" vmware
fi
