#!/usr/bin/env bash
# Build the pinned upstream crypto/ssl archives for the real CharmOS target.
# Archives alone do not demonstrate a linked client or verified HTTPS.
# Use upstream's memory BIO transport. Chromium owns the socket transport;
# unsupported legacy BSD connect BIO must not call CharmOS setsockopt stubs.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium/linux-cache"
mountpoint -q "$cache" || { echo 'Run through with-cache.sh' >&2; exit 2; }
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["chromium"]["version"])' "$root/tools/chromium/upstream.json")"
source="$cache/release/chromium-$version"
output="$cache/charmos-tls"
mkdir -p "$output"
cat > "$output/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS "-ffreestanding -fno-stack-protector -nostdlibinc -D__charmos__=1 -DOPENSSL_NO_SOCK -I$root/user/libc/include -I$root/user/rt/include -I$root/include" CACHE STRING "CharmOS target flags" FORCE)
set(CMAKE_CXX_FLAGS "-ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlibinc -nostdinc++ -D__charmos__=1 -DOPENSSL_NO_SOCK -D_LIBCPP_HARDENING_MODE_DEFAULT=_LIBCPP_HARDENING_MODE_FAST -I$source/buildtools/third_party/libc++ -I$source/third_party/libc++/src/include -I$source/third_party/libc++abi/src/include -I$root/user/libc/include -I$root/user/rt/include -I$root/include" CACHE STRING "CharmOS target flags" FORCE)
EOF
cmake -S "$source/third_party/boringssl/src" -B "$output" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$output/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_NO_ASM=ON -DBUILD_TESTING=OFF
# Collect all missing runtime interfaces in one pass, rather than rebuilding
# the OS or stopping at each individual missing declaration.
cmake --build "$output" --target crypto ssl --parallel 4 -- -k 0
