# После первой загрузки: открытого текста на диске нет, заголовок — «зашифровано».
build="$1"
img="$build/disk-sysarea.img"
if grep -qa "sysarea secret" "$img"; then
  echo "sysarea2: plaintext found on disk" >&2
  exit 1
fi
# Раздел 3 — 8 МиБ, кончается за 34 сектора до конца диска (tools/mkdisk); state — байт 12 заголовка.
size=$(stat -c %s "$img")
start=$(( size - 34 * 512 - 8 * 1024 * 1024 ))
state=$(dd if="$img" bs=1 skip=$(( start + 12 )) count=1 2>/dev/null | od -An -tu1 | tr -d ' ')
if [[ "$state" != 3 ]]; then
  echo "sysarea2: header state is '$state', want 3 (encrypted)" >&2
  exit 1
fi
bash "$(dirname "$0")/swtpm.sh" "$build" keep
