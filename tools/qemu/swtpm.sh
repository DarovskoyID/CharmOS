# TPM 2.0 для QEMU (swtpm, tools/setup-wsl.sh): свежее состояние на каждый
# запуск; swtpm выходит, когда QEMU отключается. Сокет и состояние — в /tmp:
# на /mnt/* (DrvFs) Unix-сокеты не работают. Подключение — в <сценарий>.args:
# -chardev socket,path=/tmp/charmos-swtpm/sock + -device tpm-crb.
# «keep» вторым аргументом — то же состояние TPM, что в прошлый раз
# (вторая загрузка той же машины).
build="$1"
dir=/tmp/charmos-swtpm
pkill -f "swtpm socket --tpmstate dir=$dir" 2>/dev/null || true
for _ in $(seq 1 20); do pgrep -f "swtpm socket --tpmstate dir=$dir" >/dev/null || break; sleep 0.1; done
if [[ "${2:-}" == keep && -d "$dir/state" ]]; then
  rm -f "$dir/sock"
else
  rm -rf "$dir"
  mkdir -p "$dir/state"
fi
swtpm socket --tpmstate dir="$dir/state" --ctrl type=unixio,path="$dir/sock" \
  --tpm2 --terminate --daemon --log file="$build/swtpm.log" || echo "swtpm.sh: swtpm failed" >&2
for _ in $(seq 1 50); do [[ -S "$dir/sock" ]] && break; sleep 0.1; done
