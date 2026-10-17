#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["chromium"]["version"])' "$root/tools/chromium/upstream.json")"
source="$cache/release/chromium-$version"
output="$cache/charmos-tls"
[[ -f "$output/libssl.a" && -f "$root/build/user/user/libc.a" ]] || {
  echo 'Build the native OS and build-tls.sh first' >&2; exit 2;
}
flags=(--target=x86_64-unknown-elf -std=c++23 -O2 -ffreestanding -fno-exceptions
  -fno-rtti -fno-stack-protector -fno-pic -fno-pie -nostdlibinc -nostdinc++
  -D__charmos__=1 -DOPENSSL_NO_ASM -DOPENSSL_NO_SOCK
  -D_LIBCPP_HARDENING_MODE_DEFAULT=_LIBCPP_HARDENING_MODE_FAST
  -I"$source/buildtools/third_party/libc++" -I"$source/third_party/libc++/src/include"
  -I"$source/third_party/libc++abi/src/include" -I"$root/user/libc/include"
  -I"$root/user/rt/include" -I"$root/include" -I"$source/third_party/boringssl/src/include")
# The two real upstream libc++ source files needed by these archives.
for unit in algorithm verbose_abort; do
  clang++ "${flags[@]}" -D_LIBCPP_BUILDING_LIBRARY -c \
    "$source/third_party/libc++/src/src/$unit.cpp" -o "$output/$unit.obj"
done
# BoringSSL bignum division uses compiler ABI helpers. Build the three actual
# bundled compiler-rt files; do not link host libc/libstdc++/libgcc.
builtins="$source/third_party/llvm/compiler-rt/lib/builtins"
for unit in udivti3 umodti3 udivmodti4; do
  clang --target=x86_64-unknown-elf -O2 -ffreestanding -fno-stack-protector \
    -nostdlibinc -I"$root/user/libc/include" -c "$builtins/$unit.c" -o "$output/$unit.obj"
done
for app in runtime_tls runtime_https; do
clang++ "${flags[@]}" -Wall -Wextra -Werror -c "$root/user/tests/$app.cpp" -o "$output/$app.obj"
clang++ --target=x86_64-unknown-elf -nostdlib -fuse-ld=lld -static \
  -Wl,--no-dynamic-linker -T "$root/user/rt/user.ld" -Wl,-z,max-page-size=4096 \
  "$output/$app.obj" "$output/algorithm.obj" "$output/verbose_abort.obj" \
  "$output/udivti3.obj" "$output/umodti3.obj" "$output/udivmodti4.obj" \
  -Wl,--start-group "$output/libssl.a" "$output/libcrypto.a" "$root/build/user/user/libc.a" \
  -Wl,--end-group -o "$output/$app.elf"
# Keep complete debug information in the ext4 build cache, not every boot's
# initrd. Only debug sections are removed; loaded code/data remain identical.
llvm-objcopy --strip-debug "$output/$app.elf" "$root/build/user/user/$app.elf"
done
echo 'Native TLS smoke ELF linked; run tools/test.sh browser for guest validation'
