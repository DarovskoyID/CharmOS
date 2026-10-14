# Клиент на хосте для эхо-сервера CharmOS: ждёт, пока гость начнёт слушать,
# затем 10 параллельных соединений по 12000 байт (меньше окна приёма 16 КиБ: сервер однопоточный и обслуживает их по очереди) с проверкой эха (через hostfwd
# 127.0.0.1:7778 → гость:7777). Итог — в build/sock-client.log.
build="$1"
cat >"$build/sock_client.py" <<'EOF'
import socket, sys, threading, time
def attempt(tag, data):
    s = socket.create_connection(("127.0.0.1", 7778), timeout=5)
    s.settimeout(90)
    got = bytearray()
    sender = threading.Thread(target=lambda: (s.sendall(data), s.shutdown(socket.SHUT_WR)))
    sender.start()
    while True:
        d = s.recv(65536)
        if not d:
            break
        got += d
    sender.join()
    s.close()
    return bytes(got)
def one(tag, size, results):
    data = bytes(((i * 7 + tag) & 0xff) for i in range(size))
    data = bytes([tag]) + data[1:]
    good_tries = 0
    for _ in range(600):
        try:
            got = attempt(tag, data)
        except OSError:
            time.sleep(0.5)
            continue
        if len(got) == 0:          # гость ещё не слушал: hostfwd закрыл сразу
            time.sleep(0.5)
            continue
        if got == data:
            results[tag] = True
            return
        good_tries += 1            # оборванное соединение: повторить, не больше 5 раз
        if good_tries >= 5:
            break
    results[tag] = False
results = {}
first = threading.Thread(target=one, args=(0, 12000, results))
first.start(); first.join()          # первое — когда сервер поднимется
ts = [threading.Thread(target=one, args=(t, 12000, results)) for t in range(1, 10)]
for t in ts: t.start()
for t in ts: t.join()
ok = all(results.get(t) for t in range(10))
print("sock client:", "ok" if ok else "FAIL", results, flush=True)
for _ in range(60):
    try:
        v = socket.create_connection(("127.0.0.1", 7778), timeout=30)
        v.sendall(b"VERDICT ok" if ok else b"VERDICT FAIL")
        v.shutdown(socket.SHUT_WR)
        v.settimeout(30)
        while v.recv(64):
            pass
        v.close()
        break
    except OSError:
        time.sleep(1)
EOF
pkill -f "$build/sock_client.py" 2>/dev/null || true
(timeout 300 python3 "$build/sock_client.py" >"$build/sock-client.log" 2>&1 &)
