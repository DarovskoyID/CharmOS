# Эхо-сервер TCP на хосте (127.0.0.1:7777) на время сценария; гость
# подключается к 10.0.2.2:7777 (QEMU user-net отображает его на хост).
build="$1"
cat >"$build/echo_server.py" <<'EOF'
import socket, threading
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 7777))
s.listen(64)
def serve(c):
    with c:
        while True:
            d = c.recv(65536)
            if not d:
                return
            c.sendall(d)
while True:
    c, _ = s.accept()
    threading.Thread(target=serve, args=(c,), daemon=True).start()
EOF
pkill -f "$build/echo_server.py" 2>/dev/null || true
(timeout 300 python3 "$build/echo_server.py" >/dev/null 2>&1 &)
sleep 0.5
