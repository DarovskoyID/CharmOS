bash "$(dirname "$0")/system.sh" "$1"
# Клиент на хосте: ждёт сервер, скачивает /, сверяет с файлом на диске и
# сообщает итог запросом /verdict/<ok|fail> (его видно в журнале httpd).
build="$1"
cat >"$build/http_client.py" <<'EOF'
import sys, time, urllib.request, urllib.error
want = open(sys.argv[1], 'rb').read()
got = None
for _ in range(240):
    try:
        got = urllib.request.urlopen("http://127.0.0.1:18080/", timeout=5).read()
        if got:
            break
    except Exception:
        time.sleep(0.5)
verdict = "ok" if got == want else "fail"
print("http client:", verdict, len(got or b""), flush=True)
for _ in range(60):
    try:
        urllib.request.urlopen("http://127.0.0.1:18080/verdict/" + verdict, timeout=5)
    except urllib.error.HTTPError:
        break
    except Exception:
        time.sleep(0.5)
EOF
pkill -f "$build/http_client.py" 2>/dev/null || true
(timeout 300 python3 "$build/http_client.py" "$build/system-work/index.html" >"$build/http-client.log" 2>&1 &)
