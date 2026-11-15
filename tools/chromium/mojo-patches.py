"""Pinned native Mojo capability ownership; no capability-to-FD casts."""

def mojo_patches():
    patches = {
        "mojo/public/c/system/platform_handle.h": (
            "21551d225abee343a51e1f0ed50d94edd05b593a923b1808e8c2e9aef18dce9d", [
                ('#define MOJO_PLATFORM_HANDLE_TYPE_BINDER ((MojoPlatformHandleType)6)',
                 '#define MOJO_PLATFORM_HANDLE_TYPE_BINDER ((MojoPlatformHandleType)6)\n\n// Native CharmOS kernel capability, never a libc file descriptor.\n#define MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY ((MojoPlatformHandleType)7)'),
            ]),
        "mojo/public/cpp/platform/platform_handle.h": (
            "2f7d3a39ed4211ef714aa3ab99e320bc2631743ad78d293d52ba72d1f8dd7a70", [
                ('#include "mojo/public/c/system/platform_handle.h"',
                 '#include "mojo/public/c/system/platform_handle.h"\n#if BUILDFLAG(IS_CHARMOS)\n#include <charm/handle.hpp>\n#include <charm/libc.h>\n#endif'),
                ('    kNone,', '    kNone,\n#if BUILDFLAG(IS_CHARMOS)\n    kCapability,\n#endif'),
                ('  PlatformHandle(PlatformHandle&& other);',
                 '  PlatformHandle(PlatformHandle&& other);\n#if BUILDFLAG(IS_CHARMOS)\n  explicit PlatformHandle(charm::UniqueHandle capability);\n  bool is_capability() const { return type_ == Type::kCapability; }\n  const charm::UniqueHandle& GetCapability() const { return capability_; }\n  charm::UniqueHandle TakeCapability() {\n    if (type_ != Type::kCapability) return {};\n    type_ = Type::kNone;\n    return std::move(capability_);\n  }\n#endif'),
                ('#elif BUILDFLAG(IS_POSIX)\n  bool is_valid() const { return is_valid_fd(); }',
                 '#elif BUILDFLAG(IS_CHARMOS)\n  bool is_valid() const { return bool(capability_) || is_valid_fd(); }\n#elif BUILDFLAG(IS_POSIX)\n  bool is_valid() const { return is_valid_fd(); }'),
                ('  base::ScopedFD TakeFD() {\n    if (type_ == Type::kFd) {\n      type_ = Type::kNone;\n    }\n    return std::move(fd_);\n  }',
                 '  base::ScopedFD TakeFD() {\n#if BUILDFLAG(IS_CHARMOS)\n'
                 '    if (type_ == Type::kCapability) {\n'
                 '      auto capability = TakeCapability();\n'
                 '      for (int kind : {CHARM_FD_FILE, CHARM_FD_DIRECTORY, CHARM_FD_SOCKET}) {\n'
                 '        int fd = charm_fd_from_handle(capability.get(), kind);\n'
                 '        if (fd >= 0) {\n'
                 '          (void)capability.release();\n'
                 '          return base::ScopedFD(fd);\n'
                 '        }\n'
                 '      }\n'
                 '      return base::ScopedFD();\n'
                 '    }\n#endif\n'
                 '    if (type_ == Type::kFd) {\n      type_ = Type::kNone;\n    }\n    return std::move(fd_);\n  }'),
                ('  Type type_ = Type::kNone;',
                 '  Type type_ = Type::kNone;\n#if BUILDFLAG(IS_CHARMOS)\n  charm::UniqueHandle capability_;\n#endif'),
            ]),
        "mojo/public/cpp/platform/platform_handle.cc": (
            "210a6c0cbbf9a9b2e20b20e1dc405a8c7921c8efa8c5d3835dcb5e170e5b4036", [
                ('PlatformHandle::PlatformHandle() = default;',
                 '''PlatformHandle::PlatformHandle() = default;
#if BUILDFLAG(IS_CHARMOS)
PlatformHandle::PlatformHandle(charm::UniqueHandle capability)
    : type_(capability ? Type::kCapability : Type::kNone),
      capability_(std::move(capability)) {}
#endif'''),
                ('  type_ = other.type_;',
                 '  if (this == &other) return *this;\n#if BUILDFLAG(IS_CHARMOS)\n  capability_ = std::move(other.capability_);\n#endif\n  type_ = other.type_;'),
                ('  do {\n#if BUILDFLAG(IS_WIN)',
                 '''  do {
#if BUILDFLAG(IS_CHARMOS)
    if (handle.is_capability()) {
      out_handle->type = MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY;
      out_handle->value = handle.TakeCapability().release();
      break;
    }
#endif
#if BUILDFLAG(IS_WIN)'''),
                ('#if BUILDFLAG(IS_WIN)\n  if (handle->type != MOJO_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE)',
                 '''#if BUILDFLAG(IS_CHARMOS)
  if (handle->type == MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY) {
    if (!handle->value || handle->value > UINT32_MAX) return {};
    return PlatformHandle(charm::UniqueHandle::adopt(static_cast<charm::Handle>(handle->value)));
  }
  if (handle->type == MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR && handle->value > INT32_MAX)
    return {};
#endif
#if BUILDFLAG(IS_WIN)
  if (handle->type != MOJO_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE)'''),
                ('void PlatformHandle::reset() {',
                 'void PlatformHandle::reset() {\n#if BUILDFLAG(IS_CHARMOS)\n  capability_.reset();\n#endif'),
                ('void PlatformHandle::release() {',
                 'void PlatformHandle::release() {\n#if BUILDFLAG(IS_CHARMOS)\n  std::ignore = capability_.release();\n#endif'),
                ('PlatformHandle PlatformHandle::Clone() const {',
                 '''PlatformHandle PlatformHandle::Clone() const {
#if BUILDFLAG(IS_CHARMOS)
  if (is_capability()) {
    charm::UniqueHandle copy;
    if (capability_.duplicate_same(&copy) != charm::Status::Ok) return {};
    return PlatformHandle(std::move(copy));
  }
  if (type_ == Type::kNone) return {};
#endif'''),
            ]),
        "mojo/core/platform_handle_utils.cc": (
            "eaff2420829d9f7bcea03f8ac1222ad39897925163d643bdf959bcdc413611a3", [
                ('#include "build/build_config.h"',
                 '#include "build/build_config.h"\n#if BUILDFLAG(IS_CHARMOS)\n#include <charm/process.hpp>\n#endif'),
                ('#if BUILDFLAG(IS_WIN)\n  *extracted_handle = PlatformHandle(base::win::ScopedHandle(handle.Take()));',
                 '#if BUILDFLAG(IS_CHARMOS)\n  *extracted_handle = PlatformHandle(std::move(handle));\n  *extracted_readonly_handle = PlatformHandle();\n#elif BUILDFLAG(IS_WIN)\n  *extracted_handle = PlatformHandle(base::win::ScopedHandle(handle.Take()));'),
                ('#if BUILDFLAG(IS_WIN)\n  DCHECK(!readonly_handle.is_valid());',
                 '#if BUILDFLAG(IS_CHARMOS)\n  if (readonly_handle.is_valid() || !handle.is_capability()) return {};\n  return handle.TakeCapability();\n#elif BUILDFLAG(IS_WIN)\n  DCHECK(!readonly_handle.is_valid());'),
                ('  if (process_handle->struct_size < sizeof(*process_handle)) {',
                 '  if (process_handle->struct_size < sizeof(*process_handle)\n#if BUILDFLAG(IS_CHARMOS)\n      || process_handle->value > UINT32_MAX\n#endif\n  ) {'),
                ('#if BUILDFLAG(IS_WIN)\n  base::ProcessHandle out_handle;',
                 '''#if BUILDFLAG(IS_CHARMOS)
  abi::process::ProcessInfo information{};
  if (charm::process_info(in_handle, &information) != charm::Status::Ok)
    return MOJO_RESULT_INVALID_ARGUMENT;
  auto borrowed = charm::UniqueHandle::adopt(in_handle);
  charm::UniqueHandle duplicate;
  auto status = borrowed.duplicate_same(&duplicate);
  (void)borrowed.release();
  if (status != charm::Status::Ok) return MOJO_RESULT_INVALID_ARGUMENT;
  process = base::Process(duplicate.release());
#elif BUILDFLAG(IS_WIN)
  base::ProcessHandle out_handle;'''),
            ]),
        "mojo/public/cpp/system/platform_handle.cc": (
            "25b2fd4363d706e664b2f932d0671393e223661c79987532d503621a8c3a1fb3", [
                ('#else\n  platform_handles[0].type = MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;\n'
                 '  platform_handles[0].value = static_cast<uint64_t>(handle.fd.release());',
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  platform_handles[0].type = MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY;\n'
                 '  platform_handles[0].value = static_cast<uint64_t>(handle.release());\n'
                 '#else\n'
                 '  platform_handles[0].type = MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;\n'
                 '  platform_handles[0].value = static_cast<uint64_t>(handle.fd.release());'),
                ('#else\n  if (access_mode == MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE) {',
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  if (num_platform_handles != 1) {\n'
                 '    return base::subtle::PlatformSharedMemoryRegion();\n  }\n'
                 '  if (platform_handles[0].type != MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY) {\n'
                 '    return base::subtle::PlatformSharedMemoryRegion();\n  }\n'
                 '  if (!platform_handles[0].value || platform_handles[0].value > UINT32_MAX) {\n'
                 '    return base::subtle::PlatformSharedMemoryRegion();\n  }\n'
                 '  region_handle.reset(static_cast<charm::Handle>(platform_handles[0].value));\n'
                 '#else\n  if (access_mode == MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE) {'),
            ]),
        "mojo/core/ipcz_driver/shared_buffer.cc": (
            "3804209d888c6efd904bb9ac3abfadfb0becb842d3f434049894976420bbb366", [
                ('#else\n  base::ScopedFD readonly_fd;',
                 '#elif BUILDFLAG(IS_CHARMOS)\n  return handles[0].TakeCapability();\n'
                 '#else\n  base::ScopedFD readonly_fd;'),
                ('#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_ANDROID)\n  DCHECK_EQ(handles.size(), 1u);',
                 '#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHARMOS)\n  DCHECK_EQ(handles.size(), 1u);'),
            ]),
        "mojo/core/core_ipcz.cc": (
            "b474054f12c30f00333374cff24f8ece527f6a4a05bb76d4790350e7d78c8eaf", [
                ('#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID)\n'
                 '  handles[0] = PlatformHandle(std::move(region_handle.fd));',
                 '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  handles[0] = PlatformHandle(std::move(region_handle.fd));'),
            ]),
        "mojo/public/cpp/platform/platform_channel.cc": (
            "af834d21b74125b3096e4feb5c659538d491c1aa8dd92fd9aa2be3d36e7f57dc", [
                ('#include <sys/socket.h>',
                 '#include <sys/socket.h>\n#if BUILDFLAG(IS_CHARMOS)\n#include "build/charmos/native_channel.h"\n#endif'),
                ('#elif BUILDFLAG(IS_POSIX)\nvoid CreateChannel(PlatformHandle* local_endpoint,\n'
                 '                   PlatformHandle* remote_endpoint) {\n  int fds[2];',
                 '#elif BUILDFLAG(IS_CHARMOS)\nvoid CreateChannel(PlatformHandle* local_endpoint,\n'
                 '                   PlatformHandle* remote_endpoint) {\n'
                 '  charm::UniqueHandle local, remote;\n'
                 '  PCHECK(base::CreateSocketPair(&local, &remote));\n'
                 '  *local_endpoint = PlatformHandle(std::move(local));\n'
                 '  *remote_endpoint = PlatformHandle(std::move(remote));\n'
                 '  DCHECK(local_endpoint->is_valid());\n'
                 '  DCHECK(remote_endpoint->is_valid());\n'
                 '}\n'
                 '#elif BUILDFLAG(IS_POSIX)\nvoid CreateChannel(PlatformHandle* local_endpoint,\n'
                 '                   PlatformHandle* remote_endpoint) {\n  int fds[2];'),
            ]),
    }
    patches["mojo/public/cpp/platform/platform_channel_endpoint.h"] = (
        "3d26d0e1b19534639212ddba42370de512f1701a706e5b9a6a63932e1cee0e5e", [
            ('#elif BUILDFLAG(IS_POSIX)\n  using HandlePassingInfo = base::FileHandleMappingVector;',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '  using HandlePassingInfo = std::vector<base::NativeHandleTransfer>;\n'
             '#elif BUILDFLAG(IS_POSIX)\n  using HandlePassingInfo = base::FileHandleMappingVector;'),
        ])
    patches["mojo/public/cpp/platform/platform_channel_endpoint.cc"] = (
        "65f0385f7d7442d0610a08816e9bb5548f1a6fc33dede615dbd168a3374362c0", [
            ('#include "mojo/public/cpp/platform/platform_channel.h"',
             '#include "mojo/public/cpp/platform/platform_channel.h"\n'
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '#include "base/logging.h"\n'
             '#include <charm/libc.h>\n#include <charm/handle.hpp>\n#endif'),
            ('#elif BUILDFLAG(IS_POSIX) && !BUILDFLAG(MOJO_USE_APPLE_CHANNEL)\nbool IsTargetDescriptorUsed',
             '#elif BUILDFLAG(IS_POSIX) && !BUILDFLAG(MOJO_USE_APPLE_CHANNEL) && !BUILDFLAG(IS_CHARMOS)\nbool IsTargetDescriptorUsed'),
            ('#elif BUILDFLAG(IS_FUCHSIA)\n  const uint32_t id = base::LaunchOptions::AddHandleToTransfer(',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '  CHECK(platform_handle().is_capability());\n'
             '  CHECK_LT(info.size(), abi::process::kMaxStartHandlesV2);\n'
             '  uint32_t role = 0x50000000u;\n'
             '  for (;;) {\n'
             '    bool occupied = false;\n'
             '    for (const auto& entry : info) if (entry.role == role) occupied = true;\n'
             '    if (!occupied) break;\n'
             '    CHECK_LT(role, 0x5fffffffu);\n'
             '    ++role;\n'
             '  }\n'
             '  auto rights = charm::handle_rights(platform_handle().GetCapability().get());\n'
             '  CHECK(rights.ok());\n'
             '  CHECK((rights.value & (charm::rights::kTransfer | charm::rights::kDuplicate)) ==\n'
             '        (charm::rights::kTransfer | charm::rights::kDuplicate));\n'
             '  info.push_back({platform_handle().GetCapability().get(), role,\n'
             '                  static_cast<uint32_t>(rights.value)});\n'
             '  value = base::NumberToString(role);\n'
             '#elif BUILDFLAG(IS_FUCHSIA)\n  const uint32_t id = base::LaunchOptions::AddHandleToTransfer('),
            ('#elif BUILDFLAG(IS_FUCHSIA)\n  PrepareToPass(options.handles_to_transfer, value);',
             '#elif BUILDFLAG(IS_CHARMOS)\n  PrepareToPass(options.charmos_handles, value);\n'
             '#elif BUILDFLAG(IS_FUCHSIA)\n  PrepareToPass(options.handles_to_transfer, value);'),
            ('#elif BUILDFLAG(IS_FUCHSIA)\n  unsigned int handle_value = 0;',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '  uint32_t role = 0;\n'
             '  if (!base::StringToUint(value, &role) || role < 0x50000000u || role > 0x5fffffffu)\n'
             '    return {};\n'
             '  auto endpoint = charm::UniqueHandle::adopt(charm_take_start_handle(role));\n'
             '  abi::syscall::ChannelState state{};\n'
             '  if (!endpoint || charm::channel_query(endpoint.get(), &state) != charm::Status::Ok)\n'
             '    return {};\n'
             '  return PlatformChannelEndpoint(PlatformHandle(std::move(endpoint)));\n'
             '#elif BUILDFLAG(IS_FUCHSIA)\n  unsigned int handle_value = 0;'),
        ])
    patches["content/child/child_thread_impl.cc"] = (
        "023bf8bc78fd5b947e2e33af2683b36bff5df7434d8885d56afd385b64d4e2a8", [
            ('#elif BUILDFLAG(IS_POSIX)\n#if BUILDFLAG(IS_ANDROID)',
             '#elif BUILDFLAG(IS_CHARMOS)\n'
             '  endpoint = mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(\n'
             '      *base::CommandLine::ForCurrentProcess());\n'
             '#elif BUILDFLAG(IS_POSIX)\n#if BUILDFLAG(IS_ANDROID)'),
        ])
    patches["content/app/content_main.cc"] = (
        "151a5bd97cb99c68058d5fa40a0fa7cb7893fec37475fe85c91d2205f35e329a", [
            ('#include <memory>\n#include <optional>',
             '#include <memory>\n#include <optional>\n#if BUILDFLAG(IS_CHARMOS)\n#include <pthread.h>\n#include <utility>\n#endif'),
            ('NO_STACK_PROTECTOR int ContentMain(ContentMainParams params) {\n'
             '  auto runner = ContentMainRunner::Create();\n'
             '  return RunContentProcess(std::move(params), runner.get());\n'
             '}',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             'void* ContentMainOnLargeStack(void* arg) {\n'
             '  auto params = std::unique_ptr<ContentMainParams>(\n'
             '      static_cast<ContentMainParams*>(arg));\n'
             '  auto runner = ContentMainRunner::Create();\n'
             '  int result = RunContentProcess(std::move(*params), runner.get());\n'
             '  return reinterpret_cast<void*>(static_cast<intptr_t>(result));\n'
             '}\n'
             '#endif\n\n'
             'NO_STACK_PROTECTOR int ContentMain(ContentMainParams params) {\n'
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  pthread_attr_t attr;\n'
             '  if (pthread_attr_init(&attr) != 0 ||\n'
             '      pthread_attr_setstacksize(&attr, 8u << 20) != 0)\n'
             '    return 1;\n'
             '  auto args = std::make_unique<ContentMainParams>(std::move(params));\n'
             '  pthread_t thread;\n'
             '  int result = pthread_create(&thread, &attr, &ContentMainOnLargeStack, args.get());\n'
             '  pthread_attr_destroy(&attr);\n'
             '  if (result != 0) return result;\n'
             '  void* exit_code = nullptr;\n'
             '  result = pthread_join(thread, &exit_code);\n'
             '  if (result != 0) return result;\n'
             '  return static_cast<int>(reinterpret_cast<intptr_t>(exit_code));\n'
             '#else\n'
             '  auto runner = ContentMainRunner::Create();\n'
             '  return RunContentProcess(std::move(params), runner.get());\n'
             '#endif\n'
             '}'),
        ])
    patches["mojo/public/cpp/bindings/lib/message.cc"] = (
        "32f70fa823555bccbdf40833d2e52b26b7117bfd4205a44e33f6ae334969fa8b", [
            ('void Message::NotifyPeerClosureForSerializedHandles(\n'
             '    AssociatedGroupController* group_controller) {\n'
             '  const uint32_t num_ids = payload_num_interface_ids();',
             'void Message::NotifyPeerClosureForSerializedHandles(\n'
             '    AssociatedGroupController* group_controller) {\n'
             '  if (!serialized_) return;\n'
             '  const uint32_t num_ids = payload_num_interface_ids();'),
        ])
    patches["mojo/core/BUILD.gn"] = (
        "5386990c8d8d43478265ebd1e3fefce057f5afbee63d6a15482fabf2bd26e214", [
            ('    "//mojo/core/test:test_support",\n  ]\n}\n\n# Bits of the EDK library',
             '    "//mojo/core/test:test_support",\n    "//build/charmos:mojo_runtime",\n  ]\n}\n\n# Bits of the EDK library'),
            ('      "channel_posix.h",\n    ]',
             '      "channel_posix.h",\n    ]\n'
             '    if (is_charmos) {\n'
             '      sources -= [ "channel_posix.cc", "channel_posix.h" ]\n'
             '      sources += [ "//build/charmos/mojo_channel_charmos.cc" ]\n'
             '    }'),
        ])
    patches["mojo/core/ipcz_driver/shared_buffer.cc"][1].extend([
        ('#elif BUILDFLAG(IS_CHARMOS)\n  return handles[0].TakeCapability();',
         '#elif BUILDFLAG(IS_CHARMOS)\n'
         '  if (handles.size() != 1 || !handles[0].is_capability()) return {};\n'
         '  return handles[0].TakeCapability();'),
        ('    BUILDFLAG(IS_ANDROID)\n  num_handles = 1;',
         '    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHARMOS)\n  num_handles = 1;'),
    ])
    patches["mojo/core/core_ipcz.cc"][1].append((
        '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID)\n'
        '  if (buffer->region().GetMode() ==',
        '#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
        '  if (buffer->region().GetMode() =='))
    for path, digest, function in (
        ("mojo/core/ipcz_driver/transmissible_platform_handle.cc",
         "b32646db5f6b872937a5250139abd8f429ed5334ebd5ca1739e2efd8d5fec9a6",
         "TransmissiblePlatformHandle"),
        ("mojo/core/ipcz_driver/wrapped_platform_handle.cc",
         "cea3823d8dc39df5abca02d7613851e4ee5f1cb0c6cd976b906552d5be2b889c",
         "WrappedPlatformHandle"),
    ):
        patches[path] = (digest, [
            (f'bool {function}::IsSerializable() const {{\n  return true;\n}}',
             f'bool {function}::IsSerializable() const {{\n'
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  if (!handle_.is_capability()) return false;\n'
             '  auto rights = charm::handle_rights(handle_.GetCapability().get());\n'
             '  return rights.ok() && (rights.value & charm::rights::kTransfer);\n'
             '#else\n  return true;\n#endif\n}'),
        ])
    patches["mojo/core/ipcz_driver/base_shared_memory_service.cc"] = (
        "4a2b430010817b4ef29c99801985fb06ea37a23dc2a01427e2be43810da30c67", [
            ('#define SHARED_MEMORY_SERVICE_REQUIRED() \\\n'
             '  BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID)',
             '#define SHARED_MEMORY_SERVICE_REQUIRED() \\\n'
             '  BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && \\\n'
             '  !BUILDFLAG(IS_CHARMOS)'),
        ])
    return patches
