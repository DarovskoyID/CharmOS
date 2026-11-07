"""Reject a late V8 crash even if the JavaScript success line was printed."""
from pathlib import Path
import sys

serial = (Path(sys.argv[1]) / "test-m10-v8_runtime.serial").read_text(
    encoding="utf-8", errors="replace")
for marker in ("sh: v8_runtime: killed", "# Fatal", "# Debug check failed",
               "Native V8: script evaluation failed", "Native V8: dispose failed"):
    if marker in serial:
        raise SystemExit("Native V8 lifecycle rejected: " + marker)
if serial.count("Native V8: clean shutdown ok") != 1:
    raise SystemExit("Native V8 lifecycle rejected: missing or repeated completion")
print("Native V8 host: clean lifecycle without fatal errors verified")
