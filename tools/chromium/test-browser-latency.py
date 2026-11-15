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
    parser.add_argument("--frame-smoke", action="store_true",
                        help="Exercise title drag/minimize/restore/close; return before incognito")
    parser.add_argument("--close-first", action="store_true",
                        help="Exercise title close without minimize/restore")
    parser.add_argument("--https", action="store_true",
                        help="Prefix domains opened with --sites by https://")
    parser.add_argument("--history-smoke", action="store_true",
                        help="Open history and toggle the search engine through the real menu")
    parser.add_argument("--gdb-script", type=Path,
                        help="Optional symbol-based GDB script, without hardcoded addresses")
    args = parser.parse_args()
    if args.samples < 0 or args.timeout <= 0 or args.warm_limit_ms <= 0 or args.cold_limit_ms <= 0:
        parser.error("sample count must be nonnegative; timeout and limits must be positive")
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
        command = ["bash", "tools/run.sh", "--config", "browser", "--headless",
                   "--accel", "kvm", "--cmdline", "init=bin/init root=disk0p2"]
        if args.gdb_script:
            command.insert(2, "--gdb")
        guest = subprocess.Popen(
            command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL)
        debugger = None
        if args.gdb_script:
            deadline = time.monotonic() + 30
            while True:
                listeners = subprocess.run(["ss", "-ltn"], check=True,
                                           capture_output=True, text=True).stdout
                if ":1234" in listeners:
                    break
                if time.monotonic() >= deadline or guest.poll() is not None:
                    raise RuntimeError("QEMU GDB listener did not open")
                time.sleep(.1)
            with (root / "build/browser-debugger.log").open("wb") as debugger_log:
                debugger = subprocess.Popen(
                    ["gdb", "-q", "-x", str(args.gdb_script.resolve())],
                    cwd=root, stdout=debugger_log, stderr=subprocess.STDOUT)
        def wait(check, label):
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if check(text):
                    return text
                if "FATAL:" in text or "PANIC" in text:
                    raise RuntimeError(f"Browser fatal error while waiting for {label}; see {log}")
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
                # Actual pointer input: new-tab button, then overflow menu.
                monitor("mouse_move 12 -284")
                time.sleep(.15)
                monitor("mouse_button 1")
                monitor("mouse_button 0")
                wait(lambda t: "charmos-ui: new-tab 2 incognito=0" in t,
                     "user new-tab click")
                time.sleep(.5)
                subprocess.run([sys.executable, "tools/qemu-shot.py",
                                "build/browser-ui-new-tab.png"], cwd=root, check=True)
                monitor("mouse_move 353 44")
                time.sleep(.15)
                monitor("mouse_button 1")
                monitor("mouse_button 0")
                wait(lambda t: "charmos-ui: main-menu" in t, "user menu click")
                # Popup surfaces are separate Ozone widgets, not additional
                # Shell startup stages. Wait for their real canvas attachment.
                wait(lambda t: t.count("charmos-canvas: attach widget") >= 2,
                     "first menu frame")
                time.sleep(.1)
                subprocess.run([sys.executable, "tools/qemu-shot.py",
                                "build/browser-ui-menu.png"], cwd=root, check=True)
                monitor("sendkey esc 20")
                if args.close_first:
                    wait(lambda t: "wm: window 3 destroyed" in t, "menu close")
                    close_offset = len(log.read_text(errors="replace"))
                    monitor("mouse_move 15 -44")
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    wait(lambda t: "wm: browser close requested" in t[close_offset:] and
                                    "init: browser stopped" in t[close_offset:],
                         "title close without restore")
                    text = log.read_text(errors="replace")
                    passed = ("FATAL:" not in text and "PANIC" not in text and
                              "init: restarting browser" not in text)
                    report = {"close_first_passed": passed, "passed": passed}
                    result.write_text(json.dumps(report, indent=2) + "\n")
                    print(json.dumps(report, indent=2), flush=True)
                    return 0 if passed else 1
                if args.frame_smoke:
                    wait(lambda t: "wm: window 3 destroyed" in t, "menu close")
                    monitor("mouse_move -125 -44")
                    monitor("mouse_button 1")
                    monitor("mouse_move 50 30")
                    time.sleep(.05)
                    monitor("mouse_move 50 30")
                    monitor("mouse_button 0")
                    wait(lambda t: "charmos-ui: drag-move 340," in t,
                         "title drag final position")
                    time.sleep(.2)
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-ui-drag.png"], cwd=root, check=True)
                    before_minimize = log.read_text(errors="replace").count(" minimized")
                    monitor("mouse_move 96 -60")
                    time.sleep(.15)
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    wait(lambda t: t.count(" minimized") > before_minimize,
                         "title minimize button")
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-ui-minimized.png"], cwd=root, check=True)
                    monitor("sendkey alt-b")
                    wait(lambda t: len(pattern.findall(t)) >= 2, "title restore")
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-ui-restored.png"], cwd=root, check=True)
                    close_offset = len(log.read_text(errors="replace"))
                    monitor("mouse_move 47 15")
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    wait(lambda t: "wm: browser close requested" in t[close_offset:],
                         "title close button")
                    time.sleep(.5)
                    text = log.read_text(errors="replace")
                    frame_passed = (text.count("init: started browser") == 1 and
                                    "init: restarting browser" not in text and
                                    "FATAL:" not in text and "PANIC" not in text)
                    report = {"frame_smoke_passed": frame_passed,
                              "passed": frame_passed,
                              "screenshots": ["browser-ui-drag.png",
                                              "browser-ui-minimized.png",
                                              "browser-ui-restored.png"]}
                    result.write_text(json.dumps(report, indent=2) + "\n")
                    print(json.dumps(report, indent=2), flush=True)
                    return 0 if frame_passed else 1
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
                    for marker in ("charmos-ui: main-menu",
                                   "charmos-ui: new-tab 2 incognito=0",
                                   "charmos-ui: new-tab 3 incognito=1",
                                   "charmos-ui: close-tab 2"))
            for sample in range(args.samples):
                close_offset = len(log.read_text(errors="replace"))
                monitor("sendkey alt-f4")
                wait(lambda t: "wm: browser close requested" in t[close_offset:] and
                                "init: browser stopped" in t[close_offset:], "browser close")
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
                      max(warm, default=0) < args.warm_limit_ms * 1000 and
                      text.count("init: started browser") == args.samples + 1 and
                      text.count("init: browser stopped") == args.samples and
                      "init: restarting browser" not in text and
                      "init: browser failed" not in text and
                      "PANIC" not in text and "FAILED" not in text)
            report = {"samples": samples, "warm_limit_ms": args.warm_limit_ms,
                      "warm_max_ms": max(warm, default=0) / 1000,
                      "warm_mean_ms": sum(warm) / len(warm) / 1000 if warm else 0,
                      "browser_starts": text.count("init: started browser"),
                      "passed": passed}
            if args.sites:
                # After the menu the pointer is (1005,160), address field is (800,166).
                monitor("mouse_move -205 6")
                time.sleep(.1)
                for domain in args.sites:
                    address = ("https://" if args.https else "") + domain.lower()
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    monitor("sendkey ctrl-a 20")
                    time.sleep(.05)
                    for char in address:
                        keys = {".": "dot", "-": "minus", "/": "slash", ":": "shift-semicolon"}
                        monitor("sendkey " + keys.get(char, char) + " 20")
                        time.sleep(.04)
                    monitor("sendkey ret 20")
                    print("Opening site in guest:", address, flush=True)
                    time.sleep(15)
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-" + domain.replace(".", "-") + ".png"],
                                   cwd=root, check=True)
                report["site_captures"] = args.sites
                if args.history_smoke:
                    monitor("mouse_move 205 -6")
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    wait(lambda t: t.count("charmos-ui: main-menu") >= 2 and
                                   t.count("charmos-canvas: attach widget") >= 3,
                         "history menu")
                    time.sleep(.5)
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-ui-history.png"], cwd=root, check=True)
                    monitor("mouse_move -237 556")
                    time.sleep(.3)
                    selection_offset = len(log.read_text(errors="replace"))
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    wait(lambda t: re.search(r"wm: window \d+ destroyed",
                                             t[selection_offset:]),
                         "search engine selected")
                    monitor("mouse_move 237 -556")
                    time.sleep(.3)
                    monitor("mouse_button 1")
                    monitor("mouse_button 0")
                    wait(lambda t: t.count("charmos-ui: main-menu") >= 3 and
                                   t.count("charmos-canvas: attach widget") >= 4,
                         "search engine menu")
                    time.sleep(.5)
                    subprocess.run([sys.executable, "tools/qemu-shot.py",
                                    "build/browser-ui-settings.png"], cwd=root, check=True)
                    report["history_smoke_passed"] = True
            # Re-read after ALL interaction: an address-bar crash must not be
            # hidden by a successful timing check computed before navigation.
            text = log.read_text(errors="replace")
            report["browser_starts"] = text.count("init: started browser")
            report["fatal_seen"] = "FATAL:" in text or "Check failed:" in text
            report["stability_passed"] = (
                passed and report["browser_starts"] == args.samples + 1 and
                not (report["fatal_seen"] or "init: restarting browser" in text or
                     "init: browser failed" in text))
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
            if debugger is not None and debugger.poll() is None:
                debugger.terminate()
                try:
                    debugger.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    debugger.kill()
                    debugger.wait()
            if guest.poll() is None:
                guest.terminate()
                try:
                    guest.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    guest.kill()
                    guest.wait()


if __name__ == "__main__":
    raise SystemExit(main())
