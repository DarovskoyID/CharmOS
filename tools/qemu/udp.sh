# На время сценария: UDP-эхо 127.0.0.1:7779 и DNS-заглушка 127.0.0.1:5353
# (charm.test → 10.1.2.3, иное — NXDOMAIN; RFC 1035 §4.1).
build="$1"
cat >"$build/udp_servers.py" <<'EOF'
import socket, struct, threading
def echo():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 7779))
    while True:
        d, a = s.recvfrom(2048)
        s.sendto(d, a)
def dns():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 5353))
    while True:
        q, a = s.recvfrom(512)
        if len(q) < 12:
            continue
        tid = q[:2]
        pos, labels = 12, []
        while q[pos] != 0:
            l = q[pos]; labels.append(q[pos + 1:pos + 1 + l].decode()); pos += 1 + l
        question = q[12:pos + 5]
        name = ".".join(labels)
        if name == "charm.test":
            ans = b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + bytes([10, 1, 2, 3])
            hdr = tid + struct.pack(">HHHHH", 0x8180, 1, 1, 0, 0)
            s.sendto(hdr + question + ans, a)
        else:
            hdr = tid + struct.pack(">HHHHH", 0x8183, 1, 0, 0, 0)
            s.sendto(hdr + question, a)
threading.Thread(target=echo, daemon=True).start()
dns()
EOF
pkill -f "$build/udp_servers.py" 2>/dev/null || true
(timeout 300 python3 "$build/udp_servers.py" >/dev/null 2>&1 &)
sleep 0.5
