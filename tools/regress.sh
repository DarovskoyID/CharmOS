#!/usr/bin/env bash
# Полная регрессия: сборка, хост-тесты, QEMU-сценарии всех закрытых фаз.
# Ход — в build/regress.state (строка состояния: tools/regress-status.sh).
cd "$(dirname "$0")/.."
phases=(m0 m1 m2 m3 m4 m5 m6 m7 m8 m8.5)
total=0
for p in "${phases[@]}"; do total=$((total + $(ls tests/qemu/$p/*.expected | wc -l))); done
echo "$(date +%s) $total" >build/regress.state
ninja -C build 2>&1 | grep -E "error|FAILED"
ctest --test-dir build/host 2>&1 | grep "tests passed"
fails=0
for p in "${phases[@]}"; do
  out=$(tools/test.sh "$p" 300 2>&1)
  echo "$out" | grep -v "^ok"
  fails=$((fails + $(echo "$out" | grep -c "^FAIL .*: exit code\|^FAIL .*: missing" || true)))
done
result=$([[ $fails -eq 0 ]] && echo "все зелёные" || echo "есть провалы")
echo "$(head -c 10 build/regress.state) $total $result" >build/regress.state
echo "ALLDONE ($result)"
