#!/usr/bin/env bash
# Полная регрессия: сборка, хост-тесты, QEMU-сценарии всех закрытых фаз.
# Ход — в build/regress.state (строка состояния: tools/regress-status.sh).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
phases=(m0 m1 m2 m3 m4 m5 m6 m7 m8 m8.5 m9 m10)
total=0
for p in "${phases[@]}"; do total=$((total + $(ls tests/qemu/$p/*.expected | wc -l))); done
start=$(date +%s)
echo "$start $total" >build/regress.state
if ! ninja -C build >build/regress-build.log 2>&1; then
  tail -n 50 build/regress-build.log
  echo "$start $total сборка_не_прошла" >build/regress.state
  exit 1
fi
if ! ctest --test-dir build/host --output-on-failure >build/regress-host.log 2>&1; then
  cat build/regress-host.log
  echo "$start $total хостовые_тесты_не_прошли" >build/regress.state
  exit 1
fi
grep "tests passed" build/regress-host.log
fails=0
for p in "${phases[@]}"; do
  if ! tools/test.sh "$p" 300 >"build/regress-$p.log" 2>&1; then
    cat "build/regress-$p.log"
    fails=$((fails + 1))
  fi
done
result=$([[ $fails -eq 0 ]] && echo "все зелёные" || echo "есть провалы")
echo "$start $total $result" >build/regress.state
echo "ALLDONE ($result)"
[[ $fails -eq 0 ]]
