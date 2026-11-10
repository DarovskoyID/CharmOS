#!/usr/bin/env python3
"""Capture the QEMU screen (monitor socket) and write a PNG.

Usage: qemu-shot.py <output.png> [monitor-socket]
"""
import os
import socket
import struct
import sys
import time
import zlib

MON = "/tmp/charmos-mon"


def capture(path, mon):
    ppm = "/tmp/charmos-shot-%d.ppm" % os.getpid()
    try:
        os.remove(ppm)
    except OSError:
        pass
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(mon)
    s.settimeout(5)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall(("screendump %s\n" % ppm).encode())
    size = -1
    for _ in range(200):
        time.sleep(0.1)
        if os.path.exists(ppm):
            now = os.path.getsize(ppm)
            if now > 16 and now == size:
                break
            size = now
    s.close()
    with open(ppm, "rb") as f:
        data = f.read()
    try:
        os.remove(ppm)
    except OSError:
        pass
    header, rest = data.split(b"\n", 1)[0], None
    parts = data.split(b"\n", 3)
    if parts[0] != b"P6":
        raise SystemExit("not a P6 PPM")
    w, h = map(int, parts[1].split())
    rgb = parts[3]
    if len(rgb) < w * h * 3:
        raise SystemExit("short PPM")

    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)  # filter: none
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    print("screenshot: %s (%dx%d)" % (path, w, h))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    capture(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else MON)


if __name__ == "__main__":
    main()
