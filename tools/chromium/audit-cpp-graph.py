#!/usr/bin/env python3
"""Verify the native Chromium dependency closure does not compile Rust."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

if len(sys.argv) not in (3,4):
    raise SystemExit("usage: audit-cpp-graph.py SOURCE OUTPUT [TARGET]")
source, output = (Path(arg).resolve() for arg in sys.argv[1:3])
target = sys.argv[3] if len(sys.argv)==4 else "base_runtime"
if target not in ("base_runtime","graphics_runtime","canvas_runtime"):
    raise SystemExit("Unsupported audit target")
label = "//build/charmos:"+target
args = (output / "args.gn").read_text()
if "enable_rust = false\n" not in args:
    raise SystemExit("The native profile must explicitly disable Rust")
description = json.loads(subprocess.check_output([
    str(source / "buildtools/linux64/gn"), "desc", "--root=" + str(source),
    "--root-target=//build/charmos:base_port", str(output), label,
    "deps", "--all", "--format=json"], text=True))
deps = description[label]["deps"]
for dep in deps:
    if dep.startswith(("//third_party/rust/", "//build/rust:")):
        raise SystemExit("Rust dependency in native closure: " + dep)
commands = subprocess.check_output([
    "ninja", "-C", str(output), "-t", "commands", target], text=True)
for line in commands.splitlines():
    for token in shlex.split(line):
        if Path(token).name.startswith("rustc") or token.endswith(".rs"):
            raise SystemExit("Rust compilation in native commands: " + line)
report = {
    "scope": target+" dependency graph, not a completed browser",
    "rust_enabled": False,
    "dependency_count": len(deps),
    "command_count": len(commands.splitlines()),
    "args_sha256": hashlib.sha256(args.encode()).hexdigest(),
    "commands_sha256": hashlib.sha256(commands.encode()).hexdigest(),
}
(output / ("cpp-graph-report.json" if target=="base_runtime" else target+"-cpp-graph-report.json")).write_text(json.dumps(report, indent=2) + "\n")
print("C++ graph: no Rust dependencies or compile commands; "
      f"{len(deps)} dependencies, {report['command_count']} commands")
