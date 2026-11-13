#!/usr/bin/env python3
"""Measure real WM input-to-visible frames, including repeated close/reopen.

Run in WSL through with-cache.sh, after compiling init, wm and content_shell.
Starts one headless KVM guest, writes serial/JSON/PNG artifacts under build,
and refuses to overlap another QEMU. Cold launch is reported separately from
warm restoration; it is never silently excluded from the report.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--warm-limit-ms", type=float, default=500)
    parser.add_argument("--cold-limit-ms", type=float, default=500)
    parser.add_argument("--sites", nargs="*", default=[],
                        help="Optional ASCII domains to open through the real address bar")
    parser.add_argument("--ui-smoke", action="store_true",
                        help="Exercise tabs/incognito through real QEMU keyboard input")
    args = parser.parse_args()
    if args.samples < 1 or args.timeout <= 0 or args.warm_limit_ms <= 0 or args.cold_limit_ms <= 0:
        parser.error("sample count, timeout and limit must be positive")
    if any(not re.fullmatch(r"[a-zA-Z0-9.-]+", name) for name in args.sites):
        parser.error("--sites accepts plain ASCII domain names")
    root = Path(__file__).resolve().parents[2]
    if subprocess.run(["pgrep", "-x", "qemu-system-x86"],
                      stdout=subprocess.DEVNULL).returncode == 0:
        raise SystemExit("Another QEMU is running; refusing a parallel guest")
    log = root / "build/browser-latency.serial"
    result = root / "build/browser-latency.json"
    # Never leave a previous PASS report after a failed or interrupted run.
    result.unlink(missing_ok=True)
    pattern = re.compile(r"wm: browser visible (\d+) us \((warm|cold)\)")
    with log.open("wb") as output:
        guest = subprocess.Popen(
            ["bash", "tools/run.sh", "--config", "browser", "--headless",
             "--accel", "kvm", "--cmdline", "init=bin/init root=disk0p2"],
            cwd=root, stdout=output, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL)
        def wait(check, label):
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if check(text):
                    return text
                if guest.poll() is not None:
                    raise RuntimeError(f"Guest exited while waiting for {label}")
                time.sleep(.02)
            raise TimeoutError(f"Timed out waiting for {label}; see {log}")

        def monitor(command):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(5)
                sock.connect("/tmp/charmos-mon")
                sock.recv(4096)
                sock.sendall((command + "\n").encode())
                sock.recv(4096)

        try:
            desktop_log = wait(lambda t: "wm: ready" in t, "desktop")
            if "init: browser: spawning" in desktop_log or "init: started browser" in desktop_log:
                raise RuntimeError("Cold test requires an on-demand browser, without preload")
            monitor("sendkey alt-b")
            wait(lambda t: len(pattern.findall(t)) >= 1, "first visible frame")
            # A blank toolbar frame is not proof that Unicode page rendering works.
            wait(lambda t: "wm: browser Unicode probe ready" in t, "Unicode probe page")
            time.sleep(.2)
            subprocess.run([sys.executable, "tools/qemu-shot.py",
                            "build/browser-unicode-review.png"], cwd=root, check=True)
            ui_smoke_passed = True
            if args.ui_smoke:
                monitor("sendkey ctrl-t 20")
                wait(lambda t: "charmos-ui: new-tab 2 incognito=0" in t,
                     "user new tab")
                subprocess.run([sys.executable, "tools/qemu-shot.py",
                                "build/browser-ui-new-tab.png"], cwd=root, check=True)
                monitor("sendkey ctrl-shift-n 20")
                wait(lambda t: "charmos-ui: new-tab 3 incognito=1" in t,
                     "user incognito tab")
                subprocess.run([sys.executable, "tools/qemu-shot.py",
                                "build/browser-ui-incognito.png"], cwd=root, check=True)
                monitor("sendkey ctrl-w 20")
                wait(lambda t: "charmos-ui: close-tab 2" in t,
                     "user close tab")
                subprocess.run([sys.executable, "tools/qemu-shot.py",
                                "build/browser-ui-close-tab.png"], cwd=root, check=True)
                ui_smoke_passed = all(marker in log.read_text(errors="replace")
                    for marker in ("charmos-ui: new-tab 2 incognito=0",
                                   "charmos-ui: new-tab 3 incognito=1",
                                   "charmos-ui: close-tab 2"))
            for sample in range(args.samples):
                close_offset = len(log.read_text(errors="replace"))
                before = log.read_text(errors="replace").count(" minimized")
                monitor("sendkey alt-f4")
                wait(lambda t: t.count(" minimized") > before, "browser close")
                # In addition to latency, catch a client Show/Activate undoing close.
                time.sleep(.15)
                if re.search(r"wm: window \d+ restored",
                             log.read_text(errors="replace")[close_offset:]):
                    raise RuntimeError("Browser restored itself after close")
                monitor("sendkey alt-b")
                wait(lambda t: len(pattern.findall(t)) >= sample + 2, "warm frame")
            text = log.read_text(errors="replace")
            samples = [{"microseconds": int(us), "kind": kind}
                       for us, kind in pattern.findall(text)]
            warm = [s["microseconds"] for s in samples[1:]]
            passed = (len(warm) == args.samples and
                      all(s["kind"] == "warm" for s in samples[1:]) and
                      max(warm) < args.warm_limit_ms * 1000 and
                      text.count("init: started browser") == 1 and
                      "init: browser exited" not in text and
                      "PANIC" not in text and "FAILED" not in text)
            report = {"samples": samples, "warm_limit_ms": args.warm_limit_ms,
                      "warm_max_ms": max(warm) / 1000,
                      "warm_mean_ms": sum(warm) / len(warm) / 1000,
                      "browser_starts": text.count("init: started browser"),
                      "passed": passed}
            if args.sites:
                # New guest starts with pointer (640,400), window (240,96),
                # toolbar y=150..187. Move once to its address field.
                monitor("mouse_move 160 -234")
                time.sleep(.1)
                for domain in args.sites:
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    monitor("sendkey ctrl-a 20")
                    time.sleep(.05)
                    for char in domain.lower():
                        monitor("sendkey " + ("dot" if char == "." else
                                "minus" if char == "-" else char) + " 20")
                        time.sleep(.04)
                    monitor("sendkey ret 20")
                    print("Opening site in guest:", domain, flush=True)
                    time.sleep(15)
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-" + domain.replace(".", "-") + ".png"],
                                   cwd=root, check=True)
                report["site_captures"] = args.sites
            # Re-read after ALL interaction: an address-bar crash must not be
            # hidden by a successful timing check computed before navigation.
            text = log.read_text(errors="replace")
            report["browser_starts"] = text.count("init: started browser")
            report["fatal_seen"] = "FATAL:" in text or "Check failed:" in text
            report["stability_passed"] = passed and report["browser_starts"] == 1 and not (
                report["fatal_seen"] or "init: browser exited" in text)
            report["cold_limit_ms"] = args.cold_limit_ms
            report["ui_smoke_passed"] = ui_smoke_passed
            dom_ready = re.search(r"wm: browser DOM ready (\d+) us", text)
            report["dom_ready_ms"] = int(dom_ready.group(1)) / 1000 if dom_ready else None
            report["cold_target_met"] = (samples[0]["kind"] == "cold" and
                                         samples[0]["microseconds"] < args.cold_limit_ms * 1000)
            report["passed"] = (report["stability_passed"] and
                                report["cold_target_met"] and ui_smoke_passed)
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
