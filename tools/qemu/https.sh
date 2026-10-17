set -euo pipefail
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd)"
[[ -f "$build/user/user/runtime_https.elf" ]] || { echo 'Build the native HTTPS smoke first' >&2; exit 2; }
bash "$root/tools/qemu/tls.sh" "$build"
work="$build/chromium/tls-fixture"
rm -f "$work/https-ready" "$work/https-result.json"
python3 -u "$root/tools/chromium/https-fixture.py" "$work" "$PPID" >"$work/https-server.log" 2>&1 </dev/null &
server=$!
for ((i=0; i<50; ++i)); do
  [[ ! -f "$work/https-ready" ]] || exit 0
  if ! kill -0 "$server" 2>/dev/null; then cat "$work/https-server.log"; exit 1; fi
  sleep 0.1
done
echo 'HTTPS fixture did not become ready' >&2
exit 1
