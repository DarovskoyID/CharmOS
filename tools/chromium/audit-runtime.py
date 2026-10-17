#!/usr/bin/env python3
"""Inventory real BoringSSL runtime requirements; this is not a build check."""
import json
from pathlib import Path
import re

repo = Path(__file__).resolve().parents[2]
lock = json.loads((repo / "tools/chromium/upstream.json").read_text())
source = repo / "build/chromium/linux-cache/release" / ("chromium-" + lock["chromium"]["version"])
tls = source / "third_party/boringssl/src"
if not (tls / "LICENSE").is_file():
    raise SystemExit("Mount the imported source cache with with-cache.sh first")
libc = repo / "user/libc/include"
includes = {}
symbols = {}
for folder in ("crypto", "ssl", "include"):
    for path in (tls / folder).rglob("*"):
        if path.suffix not in (".h", ".cc", ".c") or "test" in path.name:
            continue
        text = path.read_text(errors="replace")
        relative = str(path.relative_to(tls))
        for header in re.findall(r"^\s*#\s*include\s*<([^>]+)>", text, re.M):
            includes.setdefault(header, []).append(relative)
        for symbol in set(re.findall(r"\bpthread_[A-Za-z0-9_]+", text)):
            symbols.setdefault(symbol, []).append(relative)
provided = (libc / "pthread.h").read_text()
report = {
    "version": lock["chromium"]["version"],
    "scope": "Static include/symbol inventory, includes inactive platform branches; not compilation or linkage proof",
    "libc_headers_present": sorted(h for h in includes if (libc / h).is_file()),
    "c_headers_to_review": {h: files[:4] for h, files in sorted(includes.items())
                            if h.endswith(".h") and not h.startswith("openssl/") and not (libc / h).is_file()},
    "pthread_symbols_missing": {s: files[:4] for s, files in sorted(symbols.items())
                                 if not re.search(r"\b" + re.escape(s) + r"\b", provided)},
    "cxx_headers": sorted(h for h in includes if "." not in h and "/" not in h),
}
output = repo / "build/chromium/runtime-audit.json"
output.write_text(json.dumps(report, indent=2) + "\n")
print(f"Runtime inventory: {output}")
print("Missing pthread symbols: " + ", ".join(report["pthread_symbols_missing"]))
