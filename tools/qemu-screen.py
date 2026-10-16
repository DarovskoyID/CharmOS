#!/usr/bin/env python3
# Снимок экрана QEMU через монитор (tools/qemu/<сценарий>.args:
# -monitor unix:/tmp/charmos-mon,server,nowait) и цвет пикселей:
#   qemu-screen.py pixel X Y [X Y ...]  ->  "screen: pixel X,Y = rrggbb"
#   qemu-screen.py monitor <команда>     ->  (sendkey a, mouse_move 10 5, ...)
# Вызывается из tools/test.sh по строке «#pixel X Y» в <сценарий>.keys.
import os
import socket
import sys
import time

MON = "/tmp/charmos-mon"
SHOT = "/tmp/charmos-shot.ppm"


def screendump():
    if os.path.exists(SHOT):
        os.remove(SHOT)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(MON)
    s.settimeout(5)
    try:
        s.recv(4096)  # приглашение монитора
    except socket.timeout:
        pass
    s.sendall(f"screendump {SHOT}\n".encode())
    for _ in range(100):  # файл пишется асинхронно
        time.sleep(0.1)
        if os.path.exists(SHOT) and os.path.getsize(SHOT) > 16:
            size = os.path.getsize(SHOT)
            time.sleep(0.2)
            if os.path.getsize(SHOT) == size:
                break
    s.close()
    with open(SHOT, "rb") as f:
        data = f.read()
    # PPM P6: "P6\n<w> <h>\n<max>\n" + RGB
    parts = data.split(b"\n", 3)
    assert parts[0] == b"P6", "not a P6 PPM"
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def monitor(cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(MON)
    s.settimeout(1)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall((cmd + "\n").encode())
    out = b""
    try:
        while True:
            d = s.recv(65536)
            if not d:
                break
            out += d
    except socket.timeout:
        pass
    s.close()
    # Ответ — в вывод (test.sh пишет его в <лог>.screen): для отладки, в .expected не нужен.
    for line in out.decode(errors="replace").splitlines():
        if line.strip() and not line.startswith(cmd[:4]) and "(qemu)" not in line:
            print("monitor: " + line.strip())


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "monitor":  # команда монитора QEMU как есть
        monitor(" ".join(sys.argv[2:]))
        return 0
    if len(sys.argv) < 4 or sys.argv[1] != "pixel" or len(sys.argv) % 2 != 0:
        print("usage: qemu-screen.py pixel X Y [X Y ...]", file=sys.stderr)
        return 2
    try:
        w, h, rgb = screendump()
    except Exception as e:  # отчёт в лог: тест увидит отсутствие строки
        print(f"screen: screendump failed: {e}")
        return 1
    print(f"screen: {w}x{h}")
    coords = list(map(int, sys.argv[2:]))
    for i in range(0, len(coords), 2):
        x, y = coords[i], coords[i + 1]
        o = (y * w + x) * 3
        print(f"screen: pixel {x},{y} = {rgb[o]:02x}{rgb[o + 1]:02x}{rgb[o + 2]:02x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
