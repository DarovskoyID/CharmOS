set -euo pipefail
build="$1"
[[ -f "$build/user/user/runtime_tls.elf" ]] || { echo 'Run tools/chromium/build-tls-smoke.sh first' >&2; exit 2; }
work="$build/chromium/tls-fixture"
mkdir -p "$work"
# Ephemeral test-only CA and server key; never imported into the OS trust store.
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -sha256 \
  -subj '/CN=CharmOS Test CA' -addext 'basicConstraints=critical,CA:TRUE' \
  -keyout "$work/ca-key.pem" -out "$work/ca.pem" >"$work/generate.log" 2>&1
openssl req -newkey rsa:2048 -nodes -subj '/CN=charmos.test' \
  -keyout "$work/server-key.pem" -out "$work/server.csr" >>"$work/generate.log" 2>&1
cat > "$work/server.ext" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:charmos.test
EOF
openssl x509 -req -in "$work/server.csr" -CA "$work/ca.pem" -CAkey "$work/ca-key.pem" \
  -set_serial 2 -days 3650 -sha256 -extfile "$work/server.ext" \
  -out "$work/server.pem" >>"$work/generate.log" 2>&1
openssl x509 -in "$work/ca.pem" -outform DER -out "$work/ca.der"
openssl x509 -in "$work/server.pem" -outform DER -out "$work/server.der"
openssl pkey -in "$work/server-key.pem" -outform DER -out "$work/key.der"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-tls.img" 96 --esp 40 --root ext2 \
  --put "2:/etc/tls-ca.der=$work/ca.der" --put "2:/etc/tls-server.der=$work/server.der" \
  --put "2:/etc/tls-key.der=$work/key.der"
