#!/usr/bin/env python3
import shlex, subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parents[2]
cache = root / "build/chromium/linux-cache"
import json
version = json.loads((root / "tools/chromium/upstream.json").read_text())["chromium"]["version"]
out = cache / "release" / f"chromium-{version}" / "out/charmos-native"
log = root / "build/content-build15.log"
line = None
for candidate in log.read_text(errors="replace").splitlines():
    if "gcc_link_wrapper.py" in candidate and '--output="./content_shell.elf"' in candidate:
        line = candidate
if not line:
    print("link command not found")
    sys.exit(1)
args = shlex.split(line)
args.append("-Wl,--error-limit=0")
proc = subprocess.run(args, cwd=out, capture_output=True, text=True)
(root / "build/link-errors-full3.log").write_text(proc.stdout + proc.stderr)
symbols = sorted({tok.split("undefined symbol: ",1)[1].split()[0]
                  for tok in (proc.stdout + proc.stderr).splitlines()
                  if "undefined symbol:" in tok})
(root / "build/link-undefined3.txt").write_text("\n".join(symbols) + "\n")
print(f"exit={proc.returncode} unique={len(symbols)}")
for s in symbols:
    print(s)
