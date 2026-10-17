#!/usr/bin/env bash
# Compile a real upstream TLS translation unit against CharmOS headers only.
# A successful syntax probe is NOT a linked TLS library or an HTTPS test.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["chromium"]["version"])' "$root/tools/chromium/upstream.json")"
source="$cache/release/chromium-$version"
tls="$source/third_party/boringssl/src"
output="$root/build/chromium/tls-probe"
mkdir -p "$output"
# Generic ELF target deliberately does not define __linux__ or use host libc.
# Chromium's own libc++ site/header files are used without changing upstream.
for unit in crypto/rand/getentropy.cc crypto/thread_pthread.cc; do
if ! clang++ --target=x86_64-unknown-elf -std=c++17 -ffreestanding -fno-exceptions \
  -fno-rtti -fno-stack-protector -nostdinc++ -nostdlibinc -DOPENSSL_NO_ASM -D__charmos__=1 \
  -D_LIBCPP_HARDENING_MODE_DEFAULT=_LIBCPP_HARDENING_MODE_FAST \
  -I"$source/buildtools/third_party/libc++" \
  -I"$source/third_party/libc++/src/include" \
  -I"$source/third_party/libc++abi/src/include" \
  -I"$root/user/libc/include" -I"$root/user/rt/include" -I"$root/include" \
  -I"$tls/include" -fsyntax-only "$tls/$unit" \
  >"$output/${unit//\//_}.log" 2>&1; then
  echo "TLS probe blocked: $unit; diagnostics: $output/${unit//\//_}.log" >&2
  exit 1
fi
echo "TLS syntax passed: $unit"
done
echo 'TLS entropy and pthread translation units syntax passed; linkage/HTTPS still unverified'
