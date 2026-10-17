#!/usr/bin/env python3
"""Reject native outputs requiring loader facilities CharmOS does not have."""
from pathlib import Path
import struct
import sys


def audit(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected little-endian ELF64")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if (kind, machine) != (2, 62):
        raise ValueError("expected static x86-64 ET_EXEC")
    entry, phoff, shoff = struct.unpack_from("<QQQ", data, 24)
    phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 54)
    if not entry or phsize != 56 or not phnum or shsize != 64 or not shnum:
        raise ValueError("missing entry point or invalid ELF tables")
    if phoff + phsize * phnum > len(data) or shoff + shsize * shnum > len(data):
        raise ValueError("truncated ELF tables")
    for i in range(phnum):
        kind = struct.unpack_from("<I", data, phoff + i * phsize)[0]
        if kind in (2, 3, 7):
            raise ValueError("unsupported dynamic/interpreter/TLS program segment")
    for i in range(shnum):
        kind, flags = struct.unpack_from("<IQ", data, shoff + i * shsize + 4)
        if kind == 6 or flags & 0x400:
            raise ValueError("unsupported dynamic/TLS section")


if __name__ == "__main__":
    try:
        audit(sys.argv[1])
    except (OSError, ValueError, struct.error) as error:
        sys.exit("CharmOS ELF audit: " + str(error))
    print("CharmOS ELF audit: static x86-64, no dynamic loader or compiler TLS")
