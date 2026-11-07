#!/usr/bin/env python3
"""Verify the native Chromium dependency closure does not compile Rust."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
from graph_cache import snapshot

if len(sys.argv) not in (3,4):
    raise SystemExit("usage: audit-cpp-graph.py SOURCE OUTPUT [TARGET]")
source, output = (Path(arg).resolve() for arg in sys.argv[1:3])
target = sys.argv[3] if len(sys.argv)==4 else "base_runtime"
if target not in ("base_runtime","graphics_runtime","canvas_runtime","ozone_runtime","mojo_runtime","v8_runtime","blink_runtime"):
    raise SystemExit("Unsupported audit target")
if target == "blink_runtime":
    label = "//build/charmos:blink_port"
    ninja_target = "blink_port"
elif target == "ozone_runtime":
    label = "//build/charmos/ozone:ozone_runtime"
    ninja_target = target
else:
    label = "//build/charmos:" + target
    ninja_target = target
args = (output / "args.gn").read_text()
if "enable_rust = false\n" not in args:
    raise SystemExit("The native profile must explicitly disable Rust")
omitted_file=output / "charmos-omitted-rust.json"
if not omitted_file.exists():
    raise SystemExit("Missing omitted-Rust dependency report: regenerate GN")
omitted=json.loads(omitted_file.read_text())
if omitted:
    raise SystemExit("Unimplemented Rust targets in native closure:\n"+"\n".join(omitted))

report_file = output / ("cpp-graph-report.json" if target=="base_runtime" else target+"-cpp-graph-report.json")
policy_hash = hashlib.sha256(Path(__file__).read_bytes() +
                            Path(__file__).with_name("graph_cache.py").read_bytes()).hexdigest()
args_hash = hashlib.sha256(args.encode()).hexdigest()
scope = target+" dependency graph, not a completed browser"
try:
    previous = json.loads(report_file.read_text())
    receipt = previous["graph_snapshot"]
    if (previous["scope"] == scope and previous["audit_policy_sha256"] == policy_hash and
        previous["args_sha256"] == args_hash and previous["rust_enabled"] is False and
        previous["omitted_rust_targets"] == [] and previous["dependency_count"] > 0 and
        previous["command_count"] > 0 and len(previous["commands_sha256"]) == 64 and
        receipt == snapshot(output, receipt["manifests"])):
        print(f"C++ graph: verified inputs unchanged; cache hit for {target}")
        raise SystemExit(0)
except (OSError, ValueError, KeyError, TypeError):
    pass
try:
    graph_before = snapshot(output)
except (OSError, ValueError):
    graph_before = None  # Never replace a full audit with a failed cache probe.
description = json.loads(subprocess.check_output([
    str(source / "buildtools/linux64/gn"), "desc", "--root=" + str(source),
    "--root-target=//build/charmos:base_port", str(output), label,
    "deps", "--all", "--format=json"], text=True))
deps = description[label]["deps"]
rust_deps=[dep for dep in deps if dep.startswith(("//third_party/rust/", "//build/rust:"))]
if rust_deps:
    raise SystemExit("Rust dependencies in native closure:\n"+"\n".join(rust_deps))
commands = subprocess.check_output([
    "ninja", "-C", str(output), "-t", "commands", ninja_target], text=True)
for line in commands.splitlines():
    for token in shlex.split(line):
        if Path(token).name.startswith("rustc") or token.endswith(".rs"):
            raise SystemExit("Rust compilation in native commands: " + line)
report = {
    "scope": scope,
    "rust_enabled": False,
    "omitted_rust_targets": omitted,
    "dependency_count": len(deps),
    "command_count": len(commands.splitlines()),
    "args_sha256": args_hash,
    "commands_sha256": hashlib.sha256(commands.encode()).hexdigest(),
    "audit_policy_sha256": policy_hash,
}
if graph_before is not None:
    if graph_before != snapshot(output, graph_before["manifests"]):
        raise SystemExit("GN/Ninja inputs changed during the dependency audit")
    report["graph_snapshot"] = graph_before
temporary = report_file.with_suffix(".json.tmp")
temporary.write_text(json.dumps(report, indent=2) + "\n")
temporary.replace(report_file)
print("C++ graph: no Rust dependencies or compile commands; "
      f"{len(deps)} dependencies, {report['command_count']} commands")
