#!/usr/bin/env python3
"""Apply a small, hash-checked downstream port to the pinned source cache."""
import hashlib
import json
from pathlib import Path
import shutil
import shlex
import subprocess

repo = Path(__file__).resolve().parents[2]
cache = repo / "build/chromium/linux-cache"
subprocess.run(["mountpoint", "-q", str(cache)], check=True)
version = json.loads((repo / "tools/chromium/upstream.json").read_text())["chromium"]["version"]
source = cache / "release" / ("chromium-" + version)
originals = cache / "platform-originals"
licenses = repo / "tools/chromium/licenses"
for name, item in json.loads((licenses / "manifest.json").read_text()).items():
    for path in (source / item["source"], licenses / name):
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise SystemExit("Unexpected dependency license: " + str(path))
patches = {
    "buildtools/third_party/libc++/BUILD.gn": (
        "1fd4eb0d6866de38b75d2f59a4d6ae74205d76b4e7781f7101f56262412d263d",
        [("  deps = [ \"//third_party/llvm-libc:llvm-libc-shared\" ]",
          "  if (is_charmos) {\n"
          "    configs -= [ \"//build/config/compiler:exceptions\" ]\n"
          "    configs += [ \"//build/config/compiler:no_exceptions\" ]\n"
          "  }\n\n  deps = [ \"//third_party/llvm-libc:llvm-libc-shared\" ]")]),
    "buildtools/third_party/libc++abi/BUILD.gn": (
        "560588f347ec4de8fc61024a6c036047ce6a6e5f1f9f2f28295816933497118f",
        [("  if (!is_tsan) {", "  if (!is_tsan && !is_charmos) {"),
         ("  defines = [ \"LIBCXXABI_SILENT_TERMINATE\" ]",
          "  defines = [ \"LIBCXXABI_SILENT_TERMINATE\" ]\n"
          "  if (is_charmos) {\n"
          "    defines += [ \"_LIBCXXABI_NO_EXCEPTIONS\" ]\n"
          "    sources -= [\n"
          "      \"//third_party/libc++abi/src/src/cxa_exception.cpp\",\n"
          "      \"//third_party/libc++abi/src/src/cxa_exception_storage.cpp\",\n"
          "      \"//third_party/libc++abi/src/src/cxa_personality.cpp\",\n"
          "      \"//third_party/libc++abi/src/src/cxa_virtual.cpp\",\n"
          "    ]\n"
          "    sources += [ \"//third_party/libc++abi/src/src/cxa_noexception.cpp\" ]\n"
          "  }"),
         ("  # private_typeinfo.cpp implements", "  if (is_charmos) {\n"
          "    configs -= [ \"//build/config/compiler:exceptions\" ]\n"
          "    configs += [ \"//build/config/compiler:no_exceptions\" ]\n"
          "  }\n\n  # private_typeinfo.cpp implements")]),
    "build/config/clang/BUILD.gn": (
        "70201760676147b8e574f56c92a5a963d17bc2665dbd36f74f847975dc3c90a3",
        [("      } else if (is_fuchsia) {\n        if (current_cpu == \"x64\") {",
          "      } else if (is_charmos) {\n        _dir = \"charmos\"\n"
          "      } else if (is_fuchsia) {\n        if (current_cpu == \"x64\") {")]),
    "build/config/BUILDCONFIG.gn": (
        "074d2a608fb415e0359758c9bbff18930168690f719c854e3769f8e5c40015c2",
        [("} else if (target_os == \"fuchsia\") {",
          "} else if (target_os == \"charmos\") {\n"
          "  assert(target_cpu == \"x64\", \"CharmOS currently supports x64\")\n"
          "  _default_toolchain = \"//build/charmos:clang_x64\"\n"
          "} else if (target_os == \"fuchsia\") {"),
         ("is_android = current_os == \"android\"",
          "is_charmos = current_os == \"charmos\"\nis_android = current_os == \"android\"")]),
    "build/build_config.h": (
        "c6e3501edea0833a2ef1b50f5cd27e151d58198af0523fa4d91ebd62a1be2039",
        [("#if defined(ANDROID)\n#define OS_ANDROID 1",
          "#if defined(__charmos__)\n#define OS_CHARMOS 1\n#elif defined(ANDROID)\n#define OS_ANDROID 1"),
         ("    defined(OS_ZOS)\n#define OS_POSIX 1",
          "    defined(OS_ZOS) || defined(OS_CHARMOS)\n#define OS_POSIX 1"),
         ("// OS build flags\n", "// OS build flags\n"
          "#if defined(OS_CHARMOS)\n#define BUILDFLAG_INTERNAL_IS_CHARMOS() (1)\n"
          "#else\n#define BUILDFLAG_INTERNAL_IS_CHARMOS() (0)\n#endif\n\n")]),
}
operations = []
for name, (digest, replacements) in patches.items():
    target, backup = source / name, originals / name
    current = target.read_bytes()
    original = backup.read_bytes() if backup.exists() else current
    if hashlib.sha256(original).hexdigest() != digest:
        raise SystemExit("Unexpected upstream/original content: " + name)
    modified = original.decode()
    for before, after in replacements:
        if modified.count(before) != 1:
            raise SystemExit("Non-unique patch context: " + name)
        modified = modified.replace(before, after, 1)
    modified = modified.encode()
    if current not in (original, modified):
        raise SystemExit("Refusing to replace unknown local vendor edits: " + name)
    operations.append((target, backup, original, modified))
for target, backup, original, modified in operations:
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_bytes(original)
    if target.read_bytes() != modified:
        temporary = target.with_name(target.name + ".charmos-tmp")
        temporary.write_bytes(modified)
        temporary.replace(target)
overlay = repo / "tools/chromium/platform"
destination = source / "build/charmos"
destination.mkdir(exist_ok=True)
revision = hashlib.sha256((repo / "tools/chromium/toolchain.py").read_bytes()
                          + (repo / "tools/chromium/audit-elf.py").read_bytes()).hexdigest()[:16]
revision_file = destination / "toolchain-revision.gni"
revision_content = 'charmos_toolchain_revision = "' + revision + '"\n'
if not revision_file.exists() or revision_file.read_text() != revision_content:
    revision_file.write_text(revision_content)
for path in overlay.iterdir():
    if path.is_file():
        output = destination / path.name
        if not output.exists() or output.read_bytes() != path.read_bytes():
            shutil.copyfile(path, output)
for mode in ("cc", "cxx", "link", "ar"):
    wrapper = destination / (mode + "-" + revision)
    content = "#!/bin/sh\nexec python3 " + shlex.quote(str(repo / "tools/chromium/toolchain.py")) + " " + mode + ' "$@"\n'
    if not wrapper.exists() or wrapper.read_text() != content:
        wrapper.write_text(content)
    wrapper.chmod(0o755)
print("Pinned CharmOS platform prepared: " + str(destination))
