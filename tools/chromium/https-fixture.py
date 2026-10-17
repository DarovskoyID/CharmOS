#!/usr/bin/env python3
"""Bounded, loopback-only HTTPS counterpart for the native guest test."""
import json
from pathlib import Path
import socket
import ssl
import sys
import time

work = Path(sys.argv[1])
owner = Path("/proc") / sys.argv[2] / "stat"
def owner_start():
    try:
        return owner.read_text().rsplit(")", 1)[1].split()[19]
    except (FileNotFoundError, ProcessLookupError):
        return None
owner_identity = owner_start()
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = ssl.TLSVersion.TLSv1_2
context.load_cert_chain(work / "server.pem", work / "server-key.pem")
result = {"requests": 0, "rejected": 0, "unexpected": [], "versions": [], "server_names": []}
observed_names = []
context.set_servername_callback(lambda _stream, name, _ctx: observed_names.append(name))
with socket.socket() as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 17443))
    listener.listen(8)
    listener.settimeout(1)
    (work / "https-ready").write_text("ready\n")
    deadline = time.monotonic() + 300
    while (result["requests"] + result["rejected"] < 6 and time.monotonic() < deadline
           and owner_identity is not None and owner_start() == owner_identity):
        try:
            connection, _ = listener.accept()
        except TimeoutError:
            continue
        connection.settimeout(60)
        observed_names = []
        try:
            with context.wrap_socket(connection, server_side=True) as stream:
                request = b""
                while b"\r\n\r\n" not in request and len(request) < 4096:
                    block = stream.recv(4096)
                    if not block:
                        break
                    request += block
                if not request:
                    result["rejected"] += 1
                    continue
                if request != b"GET /native-test HTTP/1.0\r\nHost: charmos.test\r\n\r\n":
                    result["unexpected"].append("bad HTTP request")
                    break
                stream.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK")
                result["versions"].append(stream.version())
                result["requests"] += 1
        except (ssl.SSLError, ConnectionError):
            result["rejected"] += 1
        except Exception as error:
            result["unexpected"].append(type(error).__name__)
            break
        finally:
            # HelloRetryRequest may invoke the SNI callback more than once.
            if observed_names and all(name == observed_names[0] for name in observed_names):
                result["server_names"].append(observed_names[0])
            else:
                result["unexpected"].append("missing or changed SNI")
            connection.close()
temporary = work / "https-result.json.tmp"
temporary.write_text(json.dumps(result) + "\n")
temporary.replace(work / "https-result.json")
print(json.dumps(result), flush=True)
