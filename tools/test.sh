#!/usr/bin/env bash
# QEMU-тесты фазы: tools/test.sh <фаза> [таймаут_сек]
# Для каждого сценария tests/qemu/<фаза>/<сценарий>.expected:
#   - QEMU запускается headless (с tools/qemu/<сценарий>.args и командной
#     строкой ядра из <сценарий>.cmdline, если они есть);
#   - строки .expected должны встретиться в serial-выводе в этом порядке;
#   - код выхода QEMU должен быть 33 (isa-debug-exit 0x10).
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${CHARMOS_BUILD:-$root/build}"
phase="${1:?usage: test.sh <phase> [timeout]}"
timeout_s="${2:-60}"
dir="$root/tests/qemu/$phase"
[[ -d "$dir" ]] || { echo "test.sh: no tests in $dir" >&2; exit 2; }

failed=0
for exp in "$dir"/*.expected; do
  name=$(basename "$exp" .expected)
  log="$build/test-$phase-$name.serial"
  cfg=()
  [[ -f "$root/tools/qemu/$name.args" ]] && cfg=(--config "$name")
  [[ -f "$dir/$name.cmdline" ]] && cfg+=(--cmdline "$(head -n1 "$dir/$name.cmdline" | tr -d '\r')")

  timeout "$timeout_s" "$root/tools/run.sh" --test "${cfg[@]}" </dev/null >"$log" 2>&1
  code=$?

  ok=1
  if [[ $code -ne 33 ]]; then
    echo "FAIL $phase/$name: exit code $code (want 33; 124 = timeout)"
    ok=0
  fi
  # Ожидаемые строки по порядку.
  pos=0
  tr -d '\r' <"$log" >"$log.clean"
  while IFS= read -r want || [[ -n "$want" ]]; do
    [[ -z "$want" || "$want" == \#* ]] && continue
    line=$(tail -n +"$((pos + 1))" "$log.clean" | grep -nF -m1 -- "$want" | cut -d: -f1)
    if [[ -z "$line" ]]; then
      echo "FAIL $phase/$name: missing \"$want\""
      ok=0; break
    fi
    pos=$((pos + line))
  done <"$exp"

  if [[ $ok -eq 1 ]]; then
    echo "ok   $phase/$name"
  else
    failed=1
    echo "     serial: $log (последние 25 строк):"
    grep -av '^\s*$' "$log.clean" | tail -n 25 | sed 's/^/     | /'
  fi
done
exit $failed
