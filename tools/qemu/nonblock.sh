#!/usr/bin/env bash
# Host peer for m7/nonblock: connects but does not read until the guest has
# filled its send buffer, then drains everything so the blocking write completes.
set -euo pipefail
build="$1"
cat >"$build/nonblock_client.py" <<'EOF'
import socket, time
for attempt in range(600):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        s.settimeout(5)
        s.connect(("127.0.0.1", 7779))
        s.settimeout(60)
        break
    except OSError:
        time.sleep(0.5)
else:
    raise SystemExit("could not connect to guest")
time.sleep(1.0)
total = 0
while True:
    chunk = s.recv(65536)
    if not chunk:
        break
    total += len(chunk)
s.close()
print("nonblock client: drained", total, "bytes", flush=True)
EOF
pkill -f "$build/nonblock_client.py" 2>/dev/null || true
(timeout 120 python3 "$build/nonblock_client.py" >"$build/nonblock-client.log" 2>&1 &)
