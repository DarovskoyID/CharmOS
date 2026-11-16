#!/usr/bin/env python3
"""Exercise full Chrome through real CharmOS input and capture review images.

Requires an already-built full-Chrome browser disk. Does not compile anything.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if subprocess.run(["pgrep", "-x", "qemu-system-x86"],
                      stdout=subprocess.DEVNULL).returncode == 0:
        raise SystemExit("Another QEMU is running; refusing a parallel guest")
    log = root / "build/full-chrome-ui.serial"
    result = root / "build/full-chrome-ui.json"
    result.unlink(missing_ok=True)
    with log.open("wb") as output:
        guest = subprocess.Popen(
            ["bash", "tools/run.sh", "--config", "browser", "--headless",
             "--accel", "kvm", "--cmdline", "init=bin/init root=disk0p2"],
            cwd=root, stdout=output, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            env={**os.environ, "CHARMOS_FULL_CHROME": "1",
                 "CHARMOS_BROWSER_MULTIPROCESS": "1"})

        def wait(check, label):
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if check(text):
                    return text
                if guest.poll() is not None:
                    raise RuntimeError(f"Guest exited while waiting for {label}")
                time.sleep(.05)
            raise TimeoutError(f"Timed out waiting for {label}; see {log}")

        def monitor(command):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(5)
                sock.connect("/tmp/charmos-mon")
                sock.recv(4096)
                sock.sendall((command + "\n").encode())
                sock.recv(4096)

        def capture(name):
            subprocess.run([sys.executable, "tools/qemu-shot.py",
                            "build/full-chrome-" + name + ".png"],
                           cwd=root, check=True)

        def type_text(text):
            keys = {".": "dot", "-": "minus", "/": "slash", ":": "shift-semicolon"}
            for char in text.lower():
                monitor("sendkey " + keys.get(char, char) + " 10")
                time.sleep(.015)

        def navigate(url):
            monitor("sendkey ctrl-l 20")
            monitor("sendkey ctrl-a 20")
            type_text(url)
            monitor("sendkey ret 20")

        try:
            wait(lambda t: "wm: ready" in t, "desktop")
            monitor("sendkey alt-b")
            wait(lambda t: re.search(r"wm: browser visible \d+ us", t),
                 "full Chrome first frame")
            time.sleep(3)
            capture("start")

            navigate("file:///data/tests/browser-ui-test.pdf")
            time.sleep(8)
            capture("pdf")

            navigate("https://example.com")
            time.sleep(15)
            capture("extension")

            monitor("sendkey ctrl-t 20")
            time.sleep(2)
            capture("new-tab")
            navigate("chrome://history")
            time.sleep(4)
            capture("history")
            navigate("chrome://settings")
            time.sleep(4)
            capture("settings")
            monitor("sendkey alt-left 20")
            time.sleep(3)
            capture("back")
            monitor("sendkey alt-right 20")
            time.sleep(3)
            capture("forward")
            monitor("sendkey ctrl-shift-n 20")
            time.sleep(3)
            capture("incognito")
            monitor("sendkey ctrl-w 20")
            time.sleep(1)
            capture("close-incognito")

            text = log.read_text(errors="replace")
            fatal = any(marker in text for marker in
                        ("FATAL:", "Check failed:", "PANIC", "init: browser exited"))
            captures = ("start", "pdf", "extension", "new-tab", "history",
                        "settings", "back", "forward", "incognito",
                        "close-incognito")
            report = {
                "browser_starts": text.count("init: started browser"),
                "fatal_seen": fatal,
                "captures": [str(root / "build/full-chrome-" + name + ".png")
                             for name in captures],
                "extension_visual_review_required": True,
                "pdf_visual_review_required": True,
                "history_settings_visual_review_required": True,
                "back_forward_visual_review_required": True,
            }
            report["passed"] = report["browser_starts"] == 1 and not fatal
            result.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2), flush=True)
            return 0 if report["passed"] else 1
        finally:
            if guest.poll() is None:
                guest.terminate()
                try:
                    guest.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    guest.kill()
                    guest.wait()


if __name__ == "__main__":
    raise SystemExit(main())
