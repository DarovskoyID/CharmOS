# M8.5: системный диск с закрытой областью (раздел 3) и тестовыми пакетами,
bash "$(dirname "$0")/swtpm.sh" "$1"
# собранными tools/sign: магазин (тестовый ключ), без подписи, с испорченной
# подписью, с изменённым файлом.
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$build/store-work"
sign="$build/host/tools/sign/sign"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service pkgd /bin/pkgd
console sh /bin/sh
EOF
cat >"$work/hello.toml" <<'EOF'
id = "org.charmos.hello"
name = "Hello"
version = "1.0"
exec = "bin/hello"
permissions = ["console"]
EOF
cat >"$work/free.toml" <<'EOF'
id = "org.example.free"
name = "Free"
version = "1.0"
exec = "bin/free"
permissions = ["files", "net", "console"]
EOF
"$sign" pack "$work/hello.toml" "$root/tools/keys/test-store.key" "$work/hello-store.cpk" \
  "bin/hello=$build/user/user/app_hello.elf:x" >/dev/null
"$sign" pack "$work/free.toml" - "$work/free.cpk" "bin/free=$build/user/user/app_free.elf:x" >/dev/null
python3 - "$work" <<'EOF'
import struct, sys
w = sys.argv[1]
data = bytearray(open(w + "/hello-store.cpk", "rb").read())
sig_off = struct.unpack_from("<Q", data, 48)[0]
bad = bytearray(data); bad[sig_off + 3] ^= 1          # испорчена подпись
open(w + "/hello-bad.cpk", "wb").write(bad)
data_off = struct.unpack_from("<Q", data, 32)[0]
tam = bytearray(data); tam[data_off + 100] ^= 1       # изменён файл программы
open(w + "/hello-tampered.cpk", "wb").write(tam)
EOF
puts=(--put "2:/etc/hello.txt=$work/hello" --put "2:/etc/init.conf=$work/init.conf")
for p in hello-store hello-bad hello-tampered free; do puts+=(--put "2:/pkgs/$p.cpk=$work/$p.cpk"); done
for p in init logd sh echo cat ls mkdir rm sleep pkgd corrupt; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-store.img" 128 --esp 40 --system 16 --root ext2 "${puts[@]}"
# Тестовый «магазин»: HTTP-сервер на хосте; гость видит хост как 10.0.2.2.
pkill -f "http.server 18090" 2>/dev/null || true
(cd "$work" && timeout 300 python3 -m http.server 18090 --bind 127.0.0.1 >"$build/store-http.log" 2>&1 &)
