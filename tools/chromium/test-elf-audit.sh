#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
work="$root/build/chromium/elf-audit-test"
mkdir -p "$work"
cat > "$work/plain.cpp" <<'CPP'
volatile unsigned counter;
extern "C" void _start() { for (;;) ++counter; }
CPP
cat > "$work/tls.cpp" <<'CPP'
thread_local volatile unsigned counter = 17;
extern "C" void _start() { for (;;) ++counter; }
CPP
flags=(--target=x86_64-unknown-elf -fno-exceptions -fno-rtti -nostdlib -fuse-ld=lld)
clang++ "${flags[@]}" -static -T "$root/user/rt/user.ld" "$work/plain.cpp" -o "$work/plain.elf"
# A normal linker TLS segment is needed to construct this deliberately
# unsupported image. The native script correctly refuses it at link time.
clang++ "${flags[@]}" -static -Wl,-e,_start "$work/tls.cpp" -o "$work/tls.elf"
clang++ "${flags[@]}" -fPIC -shared "$work/plain.cpp" -o "$work/shared.elf"
python3 "$root/tools/chromium/audit-elf.py" "$work/plain.elf"
python3 "$root/tools/chromium/audit-elf.py" "$root/build/user/user/platform_smoke.elf"
for name in tls shared; do
  if python3 "$root/tools/chromium/audit-elf.py" "$work/$name.elf" > "$work/$name.log" 2>&1; then
    echo "ELF audit accepted unsupported $name output" >&2
    exit 1
  fi
done
grep -q 'TLS' "$work/tls.log"
grep -q 'ET_EXEC' "$work/shared.log"
echo 'ELF audit: real native executables accepted; compiler TLS and shared objects rejected'
