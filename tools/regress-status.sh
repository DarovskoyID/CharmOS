#!/usr/bin/env bash
# Строка состояния для Claude Code: ход регрессии (tools/regress.sh).
# build/regress.state: «<время старта> <число сценариев>»; сделанные —
# serial-журналы новее старта. Оценка остатка — по средней длительности.
state=/mnt/a/CharmOS/build/regress.state
cat >/dev/null  # Claude Code передаёт JSON на stdin — не нужен
[[ -f $state ]] || { echo "CharmOS: тесты не идут"; exit 0; }
read -r start total finished <"$state"
if [[ -n ${finished:-} ]]; then echo "CharmOS: тесты закончены ($finished)"; exit 0; fi
done=$(find /mnt/a/CharmOS/build -maxdepth 1 -name 'test-*.serial' -newermt "@$start" 2>/dev/null | wc -l)
now=$(date +%s)
elapsed=$((now - start))
if [[ $done -gt 0 ]]; then
  rem=$(( elapsed * (total - done) / done ))
  printf 'CharmOS: тесты %d/%d ≈ %d мин осталось\n' "$done" "$total" $(( (rem + 59) / 60 ))
else
  printf 'CharmOS: тесты 0/%d, идёт %d с\n' "$total" "$elapsed"
fi
