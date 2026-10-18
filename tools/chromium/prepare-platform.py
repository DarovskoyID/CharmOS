#!/usr/bin/env python3
"""Apply a small, hash-checked downstream port to the pinned source cache."""
import hashlib
import json
from pathlib import Path
import shutil
import shlex
import subprocess
import re

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
    "third_party/perfetto/include/perfetto/base/time.h": (
        "d6510d1400b05c4450b73a12e944c96a7036f7525f1e742f5d572969d60afc14", [
            ('#if (PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD))',
             '#if (PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD)) || PERFETTO_BUILDFLAG(PERFETTO_OS_CHARMOS)'),
        ]),
    "third_party/abseil-cpp/absl/base/internal/sysinfo.cc": (
        "8bc219c32b705e651f9092ab1466da6d5ef4de7afeec3b0ba4567397f258551b", [
            ('#else\n\n// Fallback implementation of `GetTID` using `pthread_self`.',
             '#elif defined(__charmos__)\n\npid_t GetTID() { return gettid(); }\n\n'
             '#else\n\n// Fallback implementation of `GetTID` using `pthread_self`.'),
        ]),
    "third_party/icu/BUILD.gn": (
        "5decb2ff44125e6e95d3f6fbc1b3527176d7f2a6536d7fbdd67ff30768268220", [
            ('  if (!is_component_build) {\n    defines += [ "U_STATIC_IMPLEMENTATION" ]',
             '  if (is_charmos) {\n    defines += [ "U_HAVE_MMAP=0" ]\n  }\n\n'
             '  if (!is_component_build) {\n    defines += [ "U_STATIC_IMPLEMENTATION" ]'),
        ]),
    "base/allocator/partition_allocator/src/partition_alloc/build_config.h": (
        "a0bc70bbb196d756c14b24d9da928301273a09dddcd84f2879cec475c5675811", [
            ('#elif defined(__APPLE__)\n// Only include TargetConditionals',
             '#elif defined(__charmos__)\n#define PA_IS_CHARMOS\n#elif defined(__APPLE__)\n// Only include TargetConditionals'),
            ('    PA_BUILDFLAG(IS_ANDROID) || PA_BUILDFLAG(IS_CHROMEOS)\n#define PA_IS_POSIX',
             '    PA_BUILDFLAG(IS_ANDROID) || PA_BUILDFLAG(IS_CHROMEOS) || defined(PA_IS_CHARMOS)\n#define PA_IS_POSIX'),
        ]),
    "base/allocator/partition_allocator/src/partition_alloc/BUILD.gn": (
        "87dc5cca6a6a9b7b27f12314af04c10a4b04fde7de53eee9dfbdefc1e673253e", [
            ('if (build_with_chromium) {\n  if (is_linux || is_chromeos || is_android) {',
             'if (build_with_chromium && target_os != "charmos") {\n  if (is_linux || is_chromeos || is_android) {'),
        ]),
    "third_party/perfetto/gn/BUILD.gn": (
        "4c83f5bf4e7b556d40e707e234b8edc624a2751ca6e2b6f13585b7981b48bb84", [
            ('  } else if (build_with_chromium) {\n    public_deps = [ "//base/test:run_all_unittests" ]',
             '  } else if (build_with_chromium && enable_rust) {\n    public_deps = [ "//base/test:run_all_unittests" ]'),
            ('  } else if (build_with_chromium && enable_rust) {\n    public_deps = [ "//base/test:run_all_unittests" ]',
             '  } else if (build_with_chromium && target_os != "charmos") {\n    public_deps = [ "//base/test:run_all_unittests" ]'),
        ]),
    "third_party/boringssl/BUILD.gn": (
        "f633ee4d9d3b0628589d2b84959824e5a588c7a32a9875a3ea71e39362605f26", [
            ('if (build_with_chromium) {\n  import("//testing/libfuzzer/fuzzer_test.gni")',
             'if (build_with_chromium && enable_rust) {\n  import("//testing/libfuzzer/fuzzer_test.gni")'),
            ('  defines = [ "OPENSSL_SMALL" ]',
             '  defines = [ "OPENSSL_SMALL" ]\n'
             '  if (is_charmos) {\n    defines += [ "OPENSSL_NO_SOCK" ]\n  }'),
        ]),
    "third_party/zlib/BUILD.gn": (
        "d76982254eab7f1d0412a50859c1c516e40d2a54149c2b16289beb21f8059649", [
            ('if (build_with_chromium) {\n  test("zlib_unittests") {',
             'if (build_with_chromium && enable_rust) {\n  test("zlib_unittests") {'),
        ]),
    "base/logging/BUILD.gn": (
        "46404ae6aaf11d348966b0f35473c4c72d77a9c20b2a23eeb2cb04ea0810258a",
        [("rust_bindgen(\"log_severity_bindgen\") {\n  header = \"log_severity.h\"\n  cpp = true\n"
          "  visibility = [ \"rust_logger:*\" ]\n  deps = [ \":log_severity\" ]\n}",
          "if (enable_rust) {\n  rust_bindgen(\"log_severity_bindgen\") {\n"
          "    header = \"log_severity.h\"\n    cpp = true\n"
          "    visibility = [ \"rust_logger:*\" ]\n    deps = [ \":log_severity\" ]\n  }\n}")]),
    "third_party/libc++/src/src/new_helpers.cpp": (
        "82b6b57506edc48dcbb13eaa5bf533e680167f983a2a2cf85064c003cb8cdb38",
        [("#ifndef __GLIBCXX__\nconst nothrow_t nothrow{};",
          "#if !defined(__GLIBCXX__) && !defined(__charmos__)\nconst nothrow_t nothrow{};")]),
    "third_party/libc++abi/src/src/cxa_handlers.cpp": (
        "8f531efff0a8231177f53e5e048c4ede2d58a80e15bbefdb07f4874a0d0321fa",
        [("new_handler\nget_new_handler() noexcept\n{\n    return __libcpp_atomic_load(&__cxa_new_handler, _AO_Acquire);\n}",
          "#if !defined(__charmos__)\nnew_handler\nget_new_handler() noexcept\n{\n"
          "    return __libcpp_atomic_load(&__cxa_new_handler, _AO_Acquire);\n}\n#endif")]),
    "third_party/libc++abi/src/src/cxa_default_handlers.cpp": (
        "1984e226dbc2de1d42a5f6b4fd54669f1acec09ce7234e3c547014b2c767e92d",
        [("new_handler\nset_new_handler(new_handler handler) noexcept\n{\n    return __libcpp_atomic_exchange(&__cxa_new_handler, handler, _AO_Acq_Rel);\n}",
          "#if !defined(__charmos__)\nnew_handler\nset_new_handler(new_handler handler) noexcept\n{\n"
          "    return __libcpp_atomic_exchange(&__cxa_new_handler, handler, _AO_Acq_Rel);\n}\n#endif")]),
    "third_party/libc++/src/include/__config": (
        "8a22f6575188bd4a8af218746301b442900356d0ad5b92e5747ac1d5e3a7a8dc",
        [("#  elif defined(__wasi__) || defined(__EMSCRIPTEN__)\n#    define _LIBCPP_USING_GETENTROPY",
          "#  elif defined(__wasi__) || defined(__EMSCRIPTEN__) || defined(__charmos__)\n#    define _LIBCPP_USING_GETENTROPY")]),
    "buildtools/third_party/libc++/__config_site": (
        "fcac9dc8ea709997ab8b43af6c4e4e2a3674531c6f6303ca42eaf1e3fe189064",
        [("#define _LIBCPP_HAS_FILESYSTEM 1",
          "#if defined(__charmos__)\n#define _LIBCPP_HAS_FILESYSTEM 0\n"
          "#else\n#define _LIBCPP_HAS_FILESYSTEM 1\n#endif")]),
    "third_party/libc++/src/src/thread.cpp": (
        "1a998bb19ddf1005203b53cfce97858cdbf2c9ec30da44695641e0f8a7617fe5",
        [("#elif defined(_LIBCPP_WIN32API)\n  return static_cast<unsigned>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));",
          "#elif defined(__charmos__)\n  return 0; // C++: hardware concurrency is not yet computable.\n"
          "#elif defined(_LIBCPP_WIN32API)\n  return static_cast<unsigned>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));")]),
    "third_party/libc++/src/include/__locale_dir/locale_base_api.h": (
        "e3d83d2580307c3b75a3f77002c909450533ffeafcf08d7857c9d96ced3844f1",
        [("#  if defined(__APPLE__)",
          "#  if defined(__charmos__)\n"
          "// Portable fixed-locale API; no Fuchsia OS identity is assumed.\n"
          "#    include <__locale_dir/support/fuchsia.h>\n"
          "#  elif defined(__APPLE__)")]),
    "buildtools/third_party/libc++/BUILD.gn": (
        "1fd4eb0d6866de38b75d2f59a4d6ae74205d76b4e7781f7101f56262412d263d",
        [("  deps = [ \"//third_party/llvm-libc:llvm-libc-shared\" ]",
          "  if (is_charmos) {\n"
          "    configs -= [ \"//build/config/compiler:exceptions\" ]\n"
          "    configs += [ \"//build/config/compiler:no_exceptions\" ]\n"
          "  }\n\n  deps = [ \"//third_party/llvm-libc:llvm-libc-shared\" ]"),
         ("  if (is_apple || (!is_asan && !is_tsan && !is_msan)) {",
          "  if (is_charmos) {\n"
          "    sources -= [\n"
          "      \"//third_party/libc++/src/src/filesystem/directory_iterator.cpp\",\n"
          "      \"//third_party/libc++/src/src/filesystem/filesystem_clock.cpp\",\n"
          "      \"//third_party/libc++/src/src/filesystem/filesystem_error.cpp\",\n"
          "      \"//third_party/libc++/src/src/filesystem/operations.cpp\",\n"
          "      \"//third_party/libc++/src/src/filesystem/path.cpp\",\n"
          "    ]\n"
          "  }\n\n  if (is_apple || (!is_asan && !is_tsan && !is_msan)) {")]),
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
perfetto_os_names = ('ANDROID', 'LINUX', 'LINUX_BUT_NOT_QNX', 'WIN', 'APPLE',
                    'MAC', 'IOS', 'WASM', 'FUCHSIA', 'NACL', 'QNX', 'APPLE_TVOS', 'FREEBSD')
perfetto_charmos = '#if defined(__charmos__)\n'
perfetto_charmos += '#define PERFETTO_BUILDFLAG_DEFINE_PERFETTO_OS_CHARMOS() 1\n'
perfetto_charmos += ''.join('#define PERFETTO_BUILDFLAG_DEFINE_PERFETTO_OS_' + name + '() 0\n'
                          for name in perfetto_os_names)
perfetto_charmos += '#elif defined(__ANDROID__)'
patches['third_party/perfetto/include/perfetto/base/build_config.h'] = (
    '01c8b46ffc3aeb50287c3821249ba6c3ce662f0fe1a2f17958e28cac33bfc62c', [
        ('#if defined(__ANDROID__)', perfetto_charmos),
        ('#error OS not supported (see build_config.h)\n#endif',
         '#error OS not supported (see build_config.h)\n#endif\n'
         '#ifndef PERFETTO_BUILDFLAG_DEFINE_PERFETTO_OS_CHARMOS\n'
         '#define PERFETTO_BUILDFLAG_DEFINE_PERFETTO_OS_CHARMOS() 0\n#endif'),
    ])
operations = []
# These are the optional Rust-language bindings around the C++ base library,
# not its C++ implementation. Upstream evaluates template assertions even for
# unreachable targets, so omit unavailable language bindings in this profile.
base_name = "base/BUILD.gn"
base_digest = "3983f3e3964b9e111bf4759b53168efb34ea533124f655a7288a328b2105343c"
base_original_path = originals / base_name
base_original = (base_original_path if base_original_path.exists() else source / base_name).read_bytes()
if hashlib.sha256(base_original).hexdigest() != base_digest:
    raise SystemExit("Unexpected upstream base BUILD.gn")
base_text = base_original.decode()
base_replacements = []
binding_names = ('base_rust', 'file_path_shim', 'command_line_shim', 'feature_shim',
                 'histogram_shim', 'sequenced_task_runner_test_bridge_cpp',
                 'run_loop_shim', 'sequenced_task_runner_shim', 'i18n')
rust_target_count = 0
for match in re.finditer(r'(?m)^([ ]*)(\w+)\("([^"]+)"\) \{', base_text):
    start, cursor = match.start(), match.end() - 1
    depth, quoted, comment, escaped = 0, False, False, False
    for end in range(cursor, len(base_text)):
        char = base_text[end]
        if comment:
            if char == "\n": comment = False
        elif quoted:
            if escaped: escaped = False
            elif char == "\\": escaped = True
            elif char == '"': quoted = False
        elif char == "#": comment = True
        elif char == '"': quoted = True
        elif char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0: break
    else:
        raise SystemExit("Unterminated base language binding")
    block = base_text[start:end + 1]
    is_rust = match.group(2) in ('rust_static_library', 'rust_macro') or match.group(3) == 'base_rust'
    if is_rust:
        rust_target_count += 1
    if not (is_rust or match.group(2).endswith('test') or 'nocompile' in match.group(2) or
            match.group(3) in binding_names or
            'testonly = true' in block):
        continue
    indent = match.group(1)
    wrapped = indent + "if (enable_rust) {\n" + "\n".join("  " + line for line in block.splitlines()) + "\n" + indent + "}"
    base_replacements.append((block, wrapped))
if rust_target_count != 15:
    raise SystemExit("Unexpected number of base Rust binding targets")
base_replacements += [
    ('  deps += [\n    "logging/rust_logger",\n'
     '    "//third_party/rust/serde_json_lenient/v0_2/wrapper",\n  ]',
     '  if (enable_rust) {\n    deps += [\n      "logging/rust_logger",\n'
     '      "//third_party/rust/serde_json_lenient/v0_2/wrapper",\n    ]\n  }'),
    ('  public_deps += [ "//build/rust:cxx_cppdeps" ]',
     '  if (enable_rust) {\n    public_deps += [ "//build/rust:cxx_cppdeps" ]\n  }'),
    ('    "logging/rust_logger/print_rust_log_ffi.cc",',
     '    # Rust log producers are absent in the C++-only profile.'),
    ('    "json/json_reader.cc",',
     '    "//build/charmos/json_reader.cc",'),
]
patches[base_name] = (base_digest, base_replacements)
patches['base/logging.cc'] = (
    'f8af905b1e6e07532f5ab45f037bec61a323dc77bbc8e5e1d222514371ee3013', [
        ('#include "base/logging/rust_logger/lib.rs.h"',
         '#if !BUILDFLAG(IS_CHARMOS)\n#include "base/logging/rust_logger/lib.rs.h"\n#endif'),
        ('  internal::init_rust_logging();',
         '#if !BUILDFLAG(IS_CHARMOS)\n  internal::init_rust_logging();\n#endif'),
        ('#if !BUILDFLAG(IS_CHARMOS)\n#include "base/logging/rust_logger/lib.rs.h"\n#endif',
         '// C++-only native profile: no Rust log producers.'),
        ('#if !BUILDFLAG(IS_CHARMOS)\n  internal::init_rust_logging();\n#endif',
         '  // C++ logging is initialized above; no Rust bridge in this profile.'),
    ])
for name, (digest, replacements) in patches.items():
    target, backup = source / name, originals / name
    current = target.read_bytes()
    original = backup.read_bytes() if backup.exists() else current
    if hashlib.sha256(original).hexdigest() != digest:
        raise SystemExit("Unexpected upstream/original content: " + name)
    modified = original.decode()
    known = [original]
    for before, after in replacements:
        if modified.count(before) != 1:
            raise SystemExit("Non-unique patch context: " + name)
        modified = modified.replace(before, after, 1)
        known.append(modified.encode())
    modified = modified.encode()
    if current not in known:
        # Permit previous prepared subsets only when reversing our exact
        # replacements produces the hash-checked original byte for byte.
        restored = current.decode()
        for before, after in reversed(replacements):
            if restored.count(after) == 1:
                restored = restored.replace(after, before, 1)
        if restored.encode() != original:
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
numeric = json.loads((repo / "tools/chromium/numeric-sources.json").read_text())
if numeric["upstream_version"] != version:
    raise SystemExit("Numeric source manifest version mismatch")
numeric_file = destination / "numeric-sources.gni"
numeric_content = ""
for group, variable in (("sources", "charmos_numeric_sources"), ("engine_sources", "charmos_stdio_sources")):
    paths = []
    for name, digest in numeric[group].items():
        path = source / "third_party/llvm-libc/src" / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit("Unexpected numeric source: " + name)
        paths.append("//third_party/llvm-libc/src/" + name)
    numeric_content += variable + " = " + json.dumps(paths, indent=2) + "\n"
if not numeric_file.exists() or numeric_file.read_text() != numeric_content:
    numeric_file.write_text(numeric_content)
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
json_checks = repo / "tests/host/test_chromium_json.cpp"
json_destination = destination / "json_checks.cc"
if not json_destination.exists() or json_destination.read_bytes() != json_checks.read_bytes():
    shutil.copyfile(json_checks, json_destination)
for mode in ("cc", "cxx", "link", "ar"):
    wrapper = destination / (mode + "-" + revision)
    content = "#!/bin/sh\nexec python3 " + shlex.quote(str(repo / "tools/chromium/toolchain.py")) + " " + mode + ' "$@"\n'
    if not wrapper.exists() or wrapper.read_text() != content:
        wrapper.write_text(content)
    wrapper.chmod(0o755)
print("Pinned CharmOS platform prepared: " + str(destination))
