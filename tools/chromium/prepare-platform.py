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
    "base/process/process_metrics.cc": (
        "59c6215545c797c5680118966e2169e96ae7236e4419df21f50a2ca79df88c4a", [
            ('#else\nint ProcessMetrics::GetIdleWakeupsPerSecond() {',
             '#elif !BUILDFLAG(IS_CHARMOS)\nint ProcessMetrics::GetIdleWakeupsPerSecond() {'),
        ]),
    "base/trace_event/malloc_dump_provider.h": (
        "d3600524f697755ad85cf1ca5251b56a06a3563ab4eb1e2cac979d4c275724a2", [
            ('    BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE)\n#define MALLOC_MEMORY_TRACING_SUPPORTED',
             '    BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_CHARMOS)\n#define MALLOC_MEMORY_TRACING_SUPPORTED'),
        ]),
    "base/posix/unix_domain_socket.h": (
        "d895f01663de3907f43927c413129c9c081d2f1e660ecd55a67a229a2d3fee83", [
            ('#include <stddef.h>',
             '#include "build/build_config.h"\n#if BUILDFLAG(IS_CHARMOS)\n'
             '#include "build/charmos/native_channel.h"\n#else\n#include <stddef.h>'),
            ('#endif  // BASE_POSIX_UNIX_DOMAIN_SOCKET_H_',
             '#endif  // !BUILDFLAG(IS_CHARMOS)\n#endif  // BASE_POSIX_UNIX_DOMAIN_SOCKET_H_'),
        ]),
    "base/sync_socket.h": (
        "23999390f53dfc6292240f2fec5ca48612b690cdeedc4c734c92986d79c93b47", [
            ('#include <sys/types.h>',
             '#include <sys/types.h>\n#if BUILDFLAG(IS_CHARMOS)\n#include <memory>\n#include <charm/handle.hpp>\n#endif'),
            ('  using Handle = PlatformFile;\n  using ScopedHandle = ScopedPlatformFile;',
             '#if BUILDFLAG(IS_CHARMOS)\n  using Handle=charm::Handle;\n  using ScopedHandle=charm::UniqueHandle;\n'
             '#else\n  using Handle = PlatformFile;\n  using ScopedHandle = ScopedPlatformFile;\n#endif'),
            ('  ScopedHandle handle_;',
             '  ScopedHandle handle_;\n#if BUILDFLAG(IS_CHARMOS)\n'
             '  struct NativeState;\n  std::unique_ptr<NativeState> native_state_;\n#endif'),
        ]),
    "base/memory/shared_memory_switch.cc": (
        "f00cd09c7eb1dd1c8064423d90c2e08a87eac38f25d0b3d878a251a6ff934f06", [
            ('#include "base/unguessable_token.h"',
             '#include "base/unguessable_token.h"\n#if BUILDFLAG(IS_CHARMOS)\n'
             '#include <charm/libc.h>\n#include <charm/handle.hpp>\n#endif'),
            ('#if !BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_APPLE)\n  CHECK(launch_options != nullptr);',
             '#if !BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_CHARMOS)\n  CHECK(launch_options != nullptr);'),
            ('#elif BUILDFLAG(IS_POSIX)\n  // Serialize the key',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '  uint32_t role=0x40000000u;\n'
             '  for (;;) {\n'
             '    bool occupied=false;\n'
             '    for (const auto& entry:launch_options->charmos_handles)\n'
             '      if (entry.role==role) occupied=true;\n'
             '    if (!occupied) break;\n'
             '    CHECK_NE(role,UINT32_MAX);\n    ++role;\n  }\n'
             '  auto metadata=charm::handle_rights(shmem_handle.get());\n'
             '  CHECK(metadata.ok());\n'
             '  uint32_t rights=static_cast<uint32_t>(metadata.value);\n'
             '  if (is_read_only) rights &= ~charm::rights::kWrite;\n'
             '  charm::UniqueHandle duplicate;\n'
             '  CHECK(shmem_handle.duplicate(rights,&duplicate)==charm::Status::Ok);\n'
             '  auto owner=std::make_shared<charm::UniqueHandle>(std::move(duplicate));\n'
             '  launch_options->charmos_handles.push_back({owner->get(),role,rights});\n'
             '  launch_options->charmos_owned_handles.push_back(std::move(owner));\n'
             '  StrAppend(&serialized,{NumberToString(role),",i,"});\n'
             '#elif BUILDFLAG(IS_POSIX)\n  // Serialize the key'),
            ('#elif BUILDFLAG(IS_POSIX)\n  DCHECK_EQ(tokens[1], "i");',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '  if (tokens[1]!="i") return unexpected(SharedMemoryError::kUnexpectedHandleType);\n'
             '  if (shmem_handle<0x40000000u || shmem_handle>UINT32_MAX)\n'
             '    return unexpected(SharedMemoryError::kInvalidHandle);\n'
             '  auto scoped_handle=charm::UniqueHandle::adopt(\n'
             '      charm_take_start_handle(static_cast<uint32_t>(shmem_handle)));\n'
             '  if (!scoped_handle) return unexpected(SharedMemoryError::kInvalidHandle);\n'
             '#elif BUILDFLAG(IS_POSIX)\n  DCHECK_EQ(tokens[1], "i");'),
        ]),
    "base/files/file_util_posix.cc": (
        "8d416614ffd120590f7baf2a0ec3fb6953a2c2cdaf53eec4c0e5b974ad5660af", [
            ('#include <sys/param.h>',
             '#if !defined(__charmos__)\n#include <sys/param.h>\n#endif'),
            ('#if !BUILDFLAG(IS_IOS)\n#include <grp.h>',
             '#if !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_CHARMOS)\n#include <grp.h>'),
            ('bool GetPosixFilePermissions(const FilePath& path, int* mode) {',
             'bool GetPosixFilePermissions(const FilePath& path, int* mode) {\n'
             '#if BUILDFLAG(IS_CHARMOS)\n  errno=ENOTSUP;\n  return false;\n#else'),
            ('  *mode = file_info.st_mode & FILE_PERMISSION_MASK;\n  return true;\n}',
             '  *mode = file_info.st_mode & FILE_PERMISSION_MASK;\n  return true;\n#endif\n}'),
            ('bool SetPosixFilePermissions(const FilePath& path, int mode) {',
             'bool SetPosixFilePermissions(const FilePath& path, int mode) {\n'
             '#if BUILDFLAG(IS_CHARMOS)\n  errno=ENOTSUP;\n  return false;\n#else'),
            ('  return true;\n}\n\nbool ExecutableExistsInPath(',
             '  return true;\n#endif\n}\n\nbool ExecutableExistsInPath('),
            ('bool AllocateFileRegion(File* file, int64_t offset, size_t size) {',
             '#if !BUILDFLAG(IS_CHARMOS)\nbool AllocateFileRegion(File* file, int64_t offset, size_t size) {'),
            ('bool AppendToFile(const FilePath& filename, span<const uint8_t> data) {',
             '#endif  // !BUILDFLAG(IS_CHARMOS)\n\nbool AppendToFile(const FilePath& filename, span<const uint8_t> data) {'),
        ]),
    "base/process/launch.h": (
        "52b4dc784b0e8678697b10c847fa03f2b482578a323fcd0574d95ef68d6ed008", [
            ('struct BASE_EXPORT LaunchOptions {',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             'struct NativeHandleTransfer { uint32_t handle; uint32_t role; uint32_t rights; };\n#endif\n'
             'struct BASE_EXPORT LaunchOptions {\n#if BUILDFLAG(IS_CHARMOS)\n'
             '  uint32_t charmos_capabilities=0;\n  uint32_t charmos_job=0;\n'
             '  uint32_t charmos_priority=0;\n  std::vector<NativeHandleTransfer> charmos_handles;\n#endif'),
            ('#if !BUILDFLAG(IS_APPLE)\n// Close all file descriptors,',
             '#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_CHARMOS)\n// Close all file descriptors,'),
            ('#include "build/build_config.h"',
             '#include "build/build_config.h"\n#if BUILDFLAG(IS_CHARMOS)\n'
             '#include <memory>\n#include <charm/handle.hpp>\n#endif'),
            ('  uint32_t charmos_priority=0;\n  std::vector<NativeHandleTransfer> charmos_handles;\n#endif',
             '  uint32_t charmos_priority=0;\n  std::vector<NativeHandleTransfer> charmos_handles;\n'
             '  std::vector<std::shared_ptr<charm::UniqueHandle>> charmos_owned_handles;\n#endif'),
        ]),
    "base/process/process_handle.h": (
        "1f1b60073b3e6bbb8ae61adb11cdb2c06fd596f017878c0db062131efd43cb04", [
            ('#elif BUILDFLAG(IS_POSIX)\n// On POSIX, our ProcessHandle will just be the PID.',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             'using ProcessHandle = int64_t;  // Native uint32 capability or self pseudo-handle.\n'
             'using ProcessId = pid_t;\n'
             'inline constexpr ProcessHandle kNullProcessHandle=0;\n'
             'inline constexpr ProcessHandle kCurrentProcessHandle=-1;\n'
             'inline constexpr ProcessId kNullProcessId=0;\n'
             '#elif BUILDFLAG(IS_POSIX)\n// On POSIX, our ProcessHandle will just be the PID.'),
        ]),
    "base/memory/discardable_shared_memory.cc": (
        "1eeb75b4ce96d3b1c331fb314e86c3ce9ecc8372919de46e9ed9cd9fc8f4ca33", [
            ('bool DiscardableSharedMemory::Purge(Time current_time) {',
             'bool DiscardableSharedMemory::Purge(Time current_time) {\n'
             '#if BUILDFLAG(IS_CHARMOS)\n  return false;  // VMO page discard is unsupported.\n#else'),
            ('  last_known_usage_ = Time();\n  return true;\n}',
             '  last_known_usage_ = Time();\n  return true;\n#endif\n}'),
        ]),
    "base/trace_event/process_memory_dump.cc": (
        "e3f8edcd27ca5b1c74ef4ca569e158fe283b5c02b1a135d8e11b7c062e2f417e", [
            ('#elif BUILDFLAG(IS_POSIX)\n    int error_counter = 0;',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '    failure=true;  // No native per-page residency query.\n'
             '    errno=ENOTSUP;\n    std::ignore=chunk_start;\n'
             '    std::ignore=page_count;\n    std::ignore=accumulate_page_if_resident;\n'
             '#elif BUILDFLAG(IS_POSIX)\n    int error_counter = 0;'),
        ]),
    "base/memory/platform_shared_memory_handle.h": (
        "0b1d68d9ddb4fb5f45e2dcf65f2d8dfd6719f2f7d59560a72d1965df438acd66", [
            ('#elif BUILDFLAG(IS_FUCHSIA)\n#include <lib/zx/vmo.h>',
             '#elif BUILDFLAG(IS_CHARMOS)\n#include <charm/handle.hpp>\n'
             '#elif BUILDFLAG(IS_FUCHSIA)\n#include <lib/zx/vmo.h>'),
            ('#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID)',
             '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)'),
            ('#elif BUILDFLAG(IS_FUCHSIA)\nusing PlatformSharedMemoryHandle',
             '#elif BUILDFLAG(IS_CHARMOS)\nusing PlatformSharedMemoryHandle = charm::Handle;\n'
             'using ScopedPlatformSharedMemoryHandle = charm::UniqueHandle;\n'
             '#elif BUILDFLAG(IS_FUCHSIA)\nusing PlatformSharedMemoryHandle'),
        ]),
    "base/memory/platform_shared_memory_handle.cc": (
        "06708570d09cd7d2763d250acab90eb316a1908fcb1070398a14a674880e1c6d", [
            ('#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID)',
             '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)'),
        ]),
    "base/memory/platform_shared_memory_region.h": (
        "393d93629641f88d42ad0d6b444bbdbf33379ae9c3a469357e7b5cd41427feeb", [
            ('#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_APPLE)',
             '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_CHARMOS)'),
            ('    kExpectedWritableButNot,',
             '    kExpectedWritableButNot,\n#if BUILDFLAG(IS_CHARMOS)\n'
             '    kInvalidNativeHandle,\n    kNativeSizeMismatch,\n#endif'),
        ]),
    "base/threading/platform_thread_posix.cc": (
        "1778a070a9635448538485f422dc17318996a5ecbffd8673613978d175a98eff", [
            ('#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_FUCHSIA)\n#include',
             '#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_CHARMOS)\n#include'),
            ('#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_FUCHSIA)\n\n// static',
             '#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_CHARMOS)\n\n// static'),
            ('#elif BUILDFLAG(IS_ANDROID)\n  // Note: do not cache',
             '#elif BUILDFLAG(IS_CHARMOS)\n  return PlatformThreadId(gettid());\n'
             '#elif BUILDFLAG(IS_ANDROID)\n  // Note: do not cache'),
        ]),
    "base/rand_util.h": (
        "cd347eebd12a828536ef1c26a4131a9f9e68cfef005a4d524f0fd78357617436", [
            ('#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE)\nBASE_EXPORT int GetUrandomFD();',
             '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_CHARMOS)\nBASE_EXPORT int GetUrandomFD();'),
        ]),
    "base/time/time.h": (
        "e66bef19df9d66cb48f6e6cc7eff68faf35dd46b29ac469206b6e7d122c1bb80", [
            ('    WIN_ROLLOVER_PROTECTED_TIME_GET_TIME\n',
             '    WIN_ROLLOVER_PROTECTED_TIME_GET_TIME,\n    CHARMOS_CLOCK_MONOTONIC\n'),
        ]),
    "base/time/time_now_posix.cc": (
        "ad8a692b7c94b84c6a8e516b61074485a3414fa419401630ad73b58d13f1ebf7", [
            ('    BUILDFLAG(IS_BSD) || BUILDFLAG(IS_ANDROID)\n',
             '    BUILDFLAG(IS_BSD) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHARMOS)\n'),
            ('  struct timezone tz = {0, 0};  // UTC\n  CHECK(gettimeofday(&tv, &tz) == 0);',
             '#if BUILDFLAG(IS_CHARMOS)\n  CHECK(gettimeofday(&tv, nullptr) == 0);\n#else\n'
             '  struct timezone tz = {0, 0};  // UTC\n  CHECK(gettimeofday(&tv, &tz) == 0);\n#endif'),
            ('  return TimeTicks() + Microseconds(ClockNow(CLOCK_MONOTONIC_COARSE));',
             '#if BUILDFLAG(IS_CHARMOS)\n  return TimeTicksNowIgnoringOverride();\n#else\n'
             '  return TimeTicks() + Microseconds(ClockNow(CLOCK_MONOTONIC_COARSE));\n#endif'),
            ('  return Clock::LINUX_CLOCK_MONOTONIC;',
             '#if BUILDFLAG(IS_CHARMOS)\n  return Clock::CHARMOS_CLOCK_MONOTONIC;\n#else\n'
             '  return Clock::LINUX_CLOCK_MONOTONIC;\n#endif'),
            ('    BUILDFLAG(IS_ANDROID)\n  return ThreadTicks()',
             '    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHARMOS)\n  return ThreadTicks()'),
        ]),
    "base/time/time_exploded_posix.cc": (
        "71b40743ad0c11933e5b6460c6119b44c8347ae477864c6a336a1fcc0a189d99", [
            ('#if !BUILDFLAG(IS_SOLARIS) && !BUILDFLAG(IS_AIX)',
             '#if !BUILDFLAG(IS_SOLARIS) && !BUILDFLAG(IS_AIX) && !BUILDFLAG(IS_CHARMOS)'),
        ]),
    "base/rand_util_posix.cc": (
        "fa4c57fc7a501cbc116dac37607d7884967eecd5c00b53465b07a4ae3d605c09", [
            ('#include <sys/syscall.h>',
             '#if !BUILDFLAG(IS_CHARMOS)\n#include <sys/syscall.h>\n#endif'),
            ('#if !BUILDFLAG(IS_APPLE)\n\n#if BUILDFLAG(IS_AIX)',
             '#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_CHARMOS)\n\n#if BUILDFLAG(IS_AIX)'),
            ('#if !BUILDFLAG(IS_APPLE)\nint GetUrandomFD()',
             '#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_CHARMOS)\nint GetUrandomFD()'),
            ('#if BUILDFLAG(IS_MAC)\n  CHECK(getentropy(output.data(), output.size()) == 0);',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  while (!output.empty()) {\n'
             '    size_t chunk=output.size()>256 ? 256 : output.size();\n'
             '    CHECK(getentropy(output.data(),chunk)==0);\n'
             '    output=output.subspan(chunk);\n  }\n'
             '#elif BUILDFLAG(IS_MAC)\n  CHECK(getentropy(output.data(), output.size()) == 0);'),
        ]),
    "base/files/file_util.cc": (
        "3a882fea594885cc3984a2fa270b95e1e91c19b208bf79cd8f4c2d626a9b84aa", [
            ('bool ContentsEqual(const FilePath& filename1, const FilePath& filename2) {',
             '#if !BUILDFLAG(IS_CHARMOS)\nbool ContentsEqual(const FilePath& filename1, const FilePath& filename2) {'),
            ('bool ReadStreamToString(FILE* stream, std::string* contents) {',
             '#endif  // !IS_CHARMOS: native streaming comparison lives in build/charmos.\n\n'
             'bool ReadStreamToString(FILE* stream, std::string* contents) {'),
        ]),
    "base/message_loop/message_pump_for_io.h": (
        "cab2ea5e73e0c88d5202d70359edff7983a7af86d94ad1780d34928abda0ef66", [
            ('#elif BUILDFLAG(IS_POSIX)\n#include "base/message_loop/message_pump_epoll.h"',
             '#elif BUILDFLAG(IS_CHARMOS)\n#include "build/charmos/message_pump_charmos.h"\n'
             '#elif BUILDFLAG(IS_POSIX)\n#include "base/message_loop/message_pump_epoll.h"'),
            ('#elif BUILDFLAG(IS_POSIX)\nusing MessagePumpForIO = MessagePumpEpoll;',
             '#elif BUILDFLAG(IS_CHARMOS)\nusing MessagePumpForIO = MessagePumpCharmOS;\n'
             '#elif BUILDFLAG(IS_POSIX)\nusing MessagePumpForIO = MessagePumpEpoll;'),
        ]),
    "base/message_loop/message_pump_for_ui.h": (
        "5d7ed92b6ed0e6ccc983036abd015bbbd6810bdfaf216b0a62d6cfbf4c5269a0", [
            ('#elif BUILDFLAG(IS_ANDROID)\n#include "base/message_loop/message_pump_android.h"',
             '#elif BUILDFLAG(IS_CHARMOS)\n#include "build/charmos/message_pump_charmos.h"\n'
             '#elif BUILDFLAG(IS_ANDROID)\n#include "base/message_loop/message_pump_android.h"'),
            ('#elif BUILDFLAG(IS_ANDROID)\nusing MessagePumpForUI = MessagePumpAndroid;',
             '#elif BUILDFLAG(IS_CHARMOS)\nusing MessagePumpForUI = MessagePumpCharmOS;\n'
             '#elif BUILDFLAG(IS_ANDROID)\nusing MessagePumpForUI = MessagePumpAndroid;'),
        ]),
    "base/debug/stack_trace.h": (
        "ea6578f820b49eb05a32db35894d106f86a20aa22591321f74209b063507c383", [
            ('#if BUILDFLAG(IS_POSIX)\n// Sets a first-chance callback',
             '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_CHARMOS)\n// Sets a first-chance callback'),
        ]),
    "base/debug/debugger_posix.cc": (
        "852e6d3b5065456d2f111375d5fef6ed111934407d17da33a7fd56bd3712f77b", [
            ('#include <sys/param.h>',
             '#if BUILDFLAG(IS_CHARMOS)\n#include <limits.h>\n#else\n#include <sys/param.h>\n#endif'),
            ('#if BUILDFLAG(IS_CHARMOS)\n#include <limits.h>\n#else\n#include <sys/param.h>\n#endif',
             '#if defined(__charmos__)\n#include <limits.h>\n#else\n#include <sys/param.h>\n#endif'),
        ]),
    "base/synchronization/lock_impl_posix.cc": (
        "69bd84e3684308cd943ea092ba6187eceed901efc8d4147dc375fe1a96a7fcd1", [
            ('#if BUILDFLAG(IS_FUCHSIA)\n#define PRIORITY_INHERITANCE_LOCKS_POSSIBLE() 0',
             '#if BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n#define PRIORITY_INHERITANCE_LOCKS_POSSIBLE() 0'),
        ]),
    "base/process/set_process_title.cc": (
        "0ee3b7e26195659bb33bf6a9a4321866b226d2931f872617675f3c4d3f8bb80d", [
            ('    !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_FUCHSIA)\n',
             '    !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_CHARMOS)\n'),
        ]),
    "base/process/process_metrics.h": (
        "bfd9dcd28d394d0969d970182c666cdc4b68eaeb7a143a91ef36e23fbb93f354", [
            ('    BUILDFLAG(IS_FUCHSIA)\n// Data about system-wide memory consumption.',
             '    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n// Data about system-wide memory consumption.'),
        ]),
    "third_party/perfetto/src/base/paged_memory.cc": (
        "48aacf758df4247a8bc45fdd217ea902e8fb54dd0c9e86800cff7e8a7eac0122", [
            ('#include "perfetto/ext/base/paged_memory.h"',
             '#if defined(__charmos__)\n#include "build/charmos/paged_memory.cc"\n#else\n'
             '#include "perfetto/ext/base/paged_memory.h"'),
            ('}  // namespace perfetto\n', '}  // namespace perfetto\n#endif  // __charmos__\n'),
        ]),
    "third_party/abseil-cpp/absl/debugging/internal/elf_mem_image.h": (
        "1b68d87c36787060222660f7031efdac8660cf06e298f8e5314986322c8570fe", [
            ('    !defined(__XTENSA__)\n#define ABSL_HAVE_ELF_MEM_IMAGE 1',
             '    !defined(__XTENSA__) && !defined(__charmos__)\n#define ABSL_HAVE_ELF_MEM_IMAGE 1'),
        ]),
    "third_party/abseil-cpp/BUILD.gn": (
        "d68cb21c4e8a53e4c3f3df9f9211bfc55c79fc4e814014b30e5392f6a8dd1031", [
            ('group("absl_component_deps") {\n  public_deps = [',
             'group("absl_component_deps") {\n  public_deps = []\n'
             '  if (!is_charmos) {\n'
             '    public_deps += [ "//third_party/abseil-cpp/absl/debugging:failure_signal_handler" ]\n'
             '  }\n  public_deps += ['),
            ('    "//third_party/abseil-cpp/absl/debugging:failure_signal_handler",',
             '    # Native CharmOS has no POSIX failure-signal/altstack API.\n'
             '    # The optional handler is not available in this profile.'),
        ]),
    "third_party/perfetto/src/base/clock_snapshots.cc": (
        "d83f631879ddf0671197963a09cfb9f068d48a310da842c62b72fc2777db758f", [
            ('      {CLOCK_REALTIME_COARSE,',
             '#if !PERFETTO_BUILDFLAG(PERFETTO_OS_CHARMOS)\n      {CLOCK_REALTIME_COARSE,'),
            ('      {CLOCK_REALTIME, protos::pbzero::BUILTIN_CLOCK_REALTIME, {0, 0}},',
             '#endif\n      {CLOCK_REALTIME, protos::pbzero::BUILTIN_CLOCK_REALTIME, {0, 0}},'),
            ('      {CLOCK_MONOTONIC_RAW,',
             '#if !PERFETTO_BUILDFLAG(PERFETTO_OS_CHARMOS)\n      {CLOCK_MONOTONIC_RAW,'),
            ('       {0, 0}},\n  };', '       {0, 0}},\n#endif\n  };'),
        ]),
    "third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_libc.cc": (
        "498b80b48fe53f8812803873cdfddb5d3dee98a42c8b345fc47ca401de1b2bfd", [
            ('#elif defined(__sun) || defined(_AIX)',
             '#elif defined(__charmos__)\n'
             '// Native libc localtime is fixed UTC; tm has no extension fields.\n'
             'long tm_gmtoff(const std::tm&) { return 0; }\n'
             'const char* tm_zone(const std::tm&) { return "UTC"; }\n'
             '#elif defined(__sun) || defined(_AIX)'),
        ]),
    "third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_info.cc": (
        "5695f30cc5842c196a358752823f6b8332ebf3cadc4f4f8a55ca044c3075ae37", [
            ('      std::ifstream version_stream(prefix + "revision.txt");\n'
             '      if (version_stream.is_open()) {\n'
             '        // revision.txt should contain no newlines, but to be\n'
             '        // defensive we read just the first line.\n'
             '        std::getline(version_stream, version);\n      }',
             '#if defined(__charmos__)\n'
             '      auto version_file = FOpen((prefix + "revision.txt").c_str());\n'
             '      if (version_file) {\n'
             '        int ch;\n'
             '        while ((ch = std::fgetc(version_file.get())) != EOF && ch != \'\\n\')\n'
             '          version.push_back(static_cast<char>(ch));\n'
             '      }\n'
             '#else\n'
             '      std::ifstream version_stream(prefix + "revision.txt");\n'
             '      if (version_stream.is_open()) {\n'
             '        // revision.txt should contain no newlines, but to be\n'
             '        // defensive we read just the first line.\n'
             '        std::getline(version_stream, version);\n      }\n'
             '#endif'),
        ]),
    "third_party/perfetto/include/perfetto/base/thread_utils.h": (
        "a2a30174d7abacbb8c2a8fc179982b6af2e66609ddbff7ebf550a587f72fced7", [
            ('#elif PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)\n#include <sys/types.h>',
             '#elif PERFETTO_BUILDFLAG(PERFETTO_OS_QNX) || PERFETTO_BUILDFLAG(PERFETTO_OS_CHARMOS)\n#include <sys/types.h>'),
            ('    PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)\nusing PlatformThreadId = pid_t;',
             '    PERFETTO_BUILDFLAG(PERFETTO_OS_QNX) || PERFETTO_BUILDFLAG(PERFETTO_OS_CHARMOS)\nusing PlatformThreadId = pid_t;'),
        ]),
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
patches["buildtools/third_party/libc++abi/BUILD.gn"][1].append((
    '    sources += [ "//third_party/libc++abi/src/src/cxa_noexception.cpp" ]',
    '    sources -= [ "//third_party/libc++abi/src/src/cxa_thread_atexit.cpp" ]\n'
    '    sources += [ "//third_party/libc++abi/src/src/cxa_noexception.cpp" ]'))
patches["buildtools/third_party/libc++abi/BUILD.gn"][1].append((
    '    sources -= [ "//third_party/libc++abi/src/src/cxa_thread_atexit.cpp" ]\n'
    '    sources += [ "//third_party/libc++abi/src/src/cxa_noexception.cpp" ]',
    '    # Native runtime owns TLS destructor support; this target does not select its pthread fallback.\n'
    '    sources += [ "//third_party/libc++abi/src/src/cxa_noexception.cpp" ]'))

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
    ('  public_deps += [\n    ":compiler_specific",\n    ":memory_dump_provider_name_variants",',
     '  if (is_charmos) {\n    sources += [ "//build/charmos/memory_metrics.cc" ]\n  }\n\n'
     '  public_deps += [\n    ":compiler_specific",\n    ":memory_dump_provider_name_variants",'),
    ('    sources += [ "//build/charmos/memory_metrics.cc" ]',
     '    sources += [\n      "//build/charmos/memory_metrics.cc",\n'
     '      "//build/charmos/message_pump_charmos.cc",\n    ]'),
    ('      "//build/charmos/message_pump_charmos.cc",',
     '      "//build/charmos/message_pump_charmos.cc",\n      "//build/charmos/files_comparison.cc",'),
    ('      "//build/charmos/files_comparison.cc",',
     '      "//build/charmos/files_comparison.cc",\n      "//build/charmos/platform_thread_charmos.cc",'),
    ('  if (is_charmos) {\n    sources += [',
     '  if (is_charmos) {\n'
     '    sources -= [ "posix/can_lower_nice_to.cc", "threading/platform_thread_internal_posix.cc" ]\n'
     '    sources += ['),
    ('      "//build/charmos/platform_thread_charmos.cc",',
     '      "//build/charmos/platform_thread_charmos.cc",\n'
     '      "//build/charmos/shared_memory_charmos.cc",\n'
     '      "//build/charmos/shared_memory_mapper_charmos.cc",'),
    ('    sources -= [ "posix/can_lower_nice_to.cc", "threading/platform_thread_internal_posix.cc" ]',
     '    sources -= [\n      "posix/can_lower_nice_to.cc",\n'
     '      "threading/platform_thread_internal_posix.cc",\n'
     '      "memory/platform_shared_memory_mapper_posix.cc",\n'
     '      "memory/platform_shared_memory_region_posix.cc",\n    ]'),
    ('      "memory/platform_shared_memory_region_posix.cc",\n    ]',
     '      "memory/platform_shared_memory_region_posix.cc",\n'
     '      "system/sys_info_posix.cc",\n      "files/drive_info_posix.cc",\n'
     '      "native_library_posix.cc",\n    ]'),
    ('      "//build/charmos/shared_memory_mapper_charmos.cc",',
     '      "//build/charmos/shared_memory_mapper_charmos.cc",\n'
     '      "//build/charmos/sys_info_charmos.cc",\n'
     '      "//build/charmos/native_library_charmos.cc",'),
    ('      "native_library_posix.cc",\n    ]',
     '      "native_library_posix.cc",\n'
     '      "process/process_handle_posix.cc",\n      "process/process_posix.cc",\n    ]'),
    ('      "//build/charmos/native_library_charmos.cc",',
     '      "//build/charmos/native_library_charmos.cc",\n      "//build/charmos/process_charmos.cc",'),
    ('      "process/process_posix.cc",\n    ]',
     '      "process/process_posix.cc",\n      "process/kill.cc",\n      "process/kill_posix.cc",\n    ]'),
    ('      "process/kill_posix.cc",\n    ]',
     '      "process/kill_posix.cc",\n      "process/launch_posix.cc",\n    ]'),
    ('      "//build/charmos/process_charmos.cc",',
     '      "//build/charmos/process_charmos.cc",\n      "//build/charmos/kill_charmos.cc",'),
    ('      "//build/charmos/kill_charmos.cc",',
     '      "//build/charmos/kill_charmos.cc",\n      "//build/charmos/launch_charmos.cc",'),
    ('      "//build/charmos/launch_charmos.cc",',
     '      "//build/charmos/launch_charmos.cc",\n      "//build/charmos/file_allocation_charmos.cc",'),
    ('      "process/launch_posix.cc",\n    ]',
     '      "process/launch_posix.cc",\n      "process/process_metrics_posix.cc",\n'
     '      "debug/stack_trace_posix.cc",\n      "profiler/module_cache_posix.cc",\n'
     '      "profiler/stack_base_address_posix.cc",\n      "profiler/stack_copier_signal.cc",\n'
     '      "profiler/thread_delegate_posix.cc",\n    ]'),
    ('      "//build/charmos/file_allocation_charmos.cc",',
     '      "//build/charmos/file_allocation_charmos.cc",\n'
     '      "//build/charmos/process_metrics_charmos.cc",\n'
     '      "//build/charmos/module_cache_charmos.cc",\n'
     '      "//build/charmos/stack_trace_charmos.cc",'),
    ('      "profiler/thread_delegate_posix.cc",\n    ]',
     '      "profiler/thread_delegate_posix.cc",\n      "trace_event/malloc_dump_provider.cc",\n    ]'),
    ('      "//build/charmos/stack_trace_charmos.cc",',
     '      "//build/charmos/stack_trace_charmos.cc",\n'
     '      "//build/charmos/malloc_dump_provider_charmos.cc",'),
    ('      "trace_event/malloc_dump_provider.cc",\n    ]',
     '      "trace_event/malloc_dump_provider.cc",\n      "sync_socket.cc",\n      "sync_socket_posix.cc",\n    ]'),
    ('      "//build/charmos/malloc_dump_provider_charmos.cc",',
     '      "//build/charmos/malloc_dump_provider_charmos.cc",\n      "//build/charmos/sync_socket_charmos.cc",'),
    ('      "trace_event/malloc_dump_provider.cc",\n      "sync_socket.cc",\n      "sync_socket_posix.cc",\n    ]',
     '      "trace_event/malloc_dump_provider.cc",\n      "sync_socket.cc",\n'
     '      "sync_socket_posix.cc",\n      "posix/unix_domain_socket.cc",\n    ]'),
    ('      "//build/charmos/sync_socket_charmos.cc",',
     '      "//build/charmos/sync_socket_charmos.cc",\n      "//build/charmos/native_channel.cc",'),
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
        ('#include <paths.h>',
         '#if !BUILDFLAG(IS_CHARMOS)\n#include <paths.h>\n#endif'),
        ('#include "base/logging/rust_logger/lib.rs.h"',
         '#if !BUILDFLAG(IS_CHARMOS)\n#include "base/logging/rust_logger/lib.rs.h"\n#endif'),
        ('  internal::init_rust_logging();',
         '#if !BUILDFLAG(IS_CHARMOS)\n  internal::init_rust_logging();\n#endif'),
        ('#if !BUILDFLAG(IS_CHARMOS)\n#include "base/logging/rust_logger/lib.rs.h"\n#endif',
         '// C++-only native profile: no Rust log producers.'),
        ('#if !BUILDFLAG(IS_CHARMOS)\n  internal::init_rust_logging();\n#endif',
         '  // C++ logging is initialized above; no Rust bridge in this profile.'),
    ])
import runpy
graphics = runpy.run_path(str(repo / "tools/chromium/graphics-patches.py"))["graphics_patches"]()
if set(graphics).intersection(patches):
    raise SystemExit("Duplicate graphics patch ownership")
patches.update(graphics)
base_replacements.append((
    '      "//build/charmos/sync_socket_charmos.cc",',
    '      "//build/charmos/sync_socket_charmos.cc",\n      "//build/charmos/allocator_charmos.cc",'))
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
            raise SystemExit("Non-unique patch context: " + name + ": " + repr(before[:180]))
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
    if group=="sources":
        for name,digest in numeric.get("fdlibm_sources",{}).items():
            if hashlib.sha256((source/name).read_bytes()).hexdigest()!=digest:
                raise SystemExit("Unexpected fdlibm source: "+name)
            paths.append("//"+name)
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
