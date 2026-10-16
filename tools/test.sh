#!/usr/bin/env bash
# QEMU-тесты фазы: tools/test.sh <фаза> [таймаут_сек]
# Для каждого сценария tests/qemu/<фаза>/<сценарий>.expected:
#   - QEMU запускается headless (с tools/qemu/<сценарий>.args и командной
#     строкой ядра из <сценарий>.cmdline, если они есть);
#   - строки .expected должны встретиться в serial-выводе в этом порядке;
#   - код выхода QEMU должен быть 33 (isa-debug-exit 0x10).
#   - <сценарий>.timeout — свой таймаут в секундах (иначе общий, по умолчанию 60).
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${CHARMOS_BUILD:-$root/build}"
phase="${1:?usage: test.sh <phase> [timeout]}"
default_timeout="${2:-60}"
dir="$root/tests/qemu/$phase"
[[ -d "$dir" ]] || { echo "test.sh: no tests in $dir" >&2; exit 2; }

failed=0
for exp in "$dir"/*.expected; do
  name=$(basename "$exp" .expected)
  log="$build/test-$phase-$name.serial"
  timeout_s=$default_timeout
  [[ -f "$dir/$name.timeout" ]] && timeout_s=$(head -n1 "$dir/$name.timeout" | tr -d '\r')
  cfg=()
  [[ -f "$root/tools/qemu/$name.args" ]] && cfg=(--config "$name")
  [[ -f "$dir/$name.cmdline" ]] && cfg+=(--cmdline "$(head -n1 "$dir/$name.cmdline" | tr -d '\r')")

  # Ввод на COM1: <сценарий>.input подаётся на stdin QEMU через 12 с (после
  # прошивки: OVMF сам читает serial), затем stdin держится открытым.
  # Ввод держится открытым, пока QEMU не вышел (флаг $done), но не дольше таймаута.
  done="$log.done"
  rm -f "$done" "$log.screen"
  hold() { for _ in $(seq 1 "$timeout_s"); do [[ -f $done ]] && return; sleep 1; done; }
  # <сценарий>.keys — ввод по шагам: строка — набрать и Enter, «^C» — Ctrl-C,
  # «#sleep N» — пауза, «#wait ТЕКСТ» — ждать ТЕКСТ в serial-выводе,
  # «#pixel X Y ...» — снимок экрана (tools/qemu-screen.py): строки
  # «screen: …» идут в конец проверяемого вывода.
  if [[ -f "$dir/$name.keys" ]]; then
    : >"$log"
    { while IFS= read -r line || [[ -n "$line" ]]; do
        line=${line%$'\r'}
        case "$line" in
          "#sleep "*) sleep "${line#\#sleep }" ;;
          "#wait "*) want=${line#\#wait }
                     for _ in $(seq 1 "$timeout_s"); do grep -qF -- "$want" "$log" && break; sleep 1; done ;;
          "#pixel "*) python3 "$root/tools/qemu-screen.py" pixel ${line#\#pixel } >>"$log.screen" ;;
          "^C") printf '\003' ;;
          *) printf '%s\n' "$line"; sleep 0.3 ;;
        esac
      done <"$dir/$name.keys"; hold; } |
      { timeout "$timeout_s" "$root/tools/run.sh" --test "${cfg[@]}" >"$log" 2>&1; c=$?; : >"$done"; exit $c; }
    code=${PIPESTATUS[1]}
  elif [[ -f "$dir/$name.input" ]]; then
    { sleep 12; cat "$dir/$name.input"; hold; } |
      { timeout "$timeout_s" "$root/tools/run.sh" --test "${cfg[@]}" >"$log" 2>&1; c=$?; : >"$done"; exit $c; }
    code=${PIPESTATUS[1]}
  else
    timeout "$timeout_s" "$root/tools/run.sh" --test "${cfg[@]}" </dev/null >"$log" 2>&1
    code=$?
  fi

  ok=1
  want=33  # isa-debug-exit; <сценарий>.exitcode — другой (например, 0 при выключении ACPI)
  [[ -f "$dir/$name.exitcode" ]] && want=$(tr -d '\r\n' <"$dir/$name.exitcode")
  if [[ $code -ne $want ]]; then
    echo "FAIL $phase/$name: exit code $code (want $want; 124 = timeout)"
    ok=0
  fi
  # Ожидаемые строки по порядку.
  pos=0
  tr -d '\r' <"$log" >"$log.clean"
  [[ -f "$log.screen" ]] && cat "$log.screen" >>"$log.clean"
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
    cp "$log" "$log.fail"  # следующий прогон перезапишет лог, а разбирать — этот
    echo "     serial: $log (последние 25 строк):"
    grep -av '^\s*$' "$log.clean" | tail -n 25 | sed 's/^/     | /'
  fi
done
exit $failed
