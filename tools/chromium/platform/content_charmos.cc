// CharmOS content-process architecture.
//
// CharmOS starts programs explicitly through charm_spawn and has no fork/exec
// or setuid/namespace sandbox. Child processes are launched directly (without
// a zygote) and receive their Mojo bootstrap endpoint as a native startup
// capability. The remaining symbols below are Linux zygote and sandbox entry
// points which upstream content still references unconditionally.
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/no_destructor.h"
#include "base/process/kill.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/process/launch.h"
#include "content/browser/child_process_launcher_helper.h"
#include "content/browser/child_process_launcher.h"
#include "content/browser/posix_file_descriptor_info_impl.h"
#include "content/browser/sandbox_host_linux.h"
#include "content/browser/sandboxed_process_launcher_delegate.h"
#include "content/browser/scheduler/responsiveness/native_event_observer.h"
#include "content/browser/zygote_host/zygote_host_impl_linux.h"
#include "content/common/zygote/zygote_communication_linux.h"
#include "content/public/browser/child_process_termination_info.h"
#include "content/public/common/zygote/sandbox_support_linux.h"
#include "content/public/common/zygote/zygote_fork_delegate_linux.h"
#include "content/renderer/renderer_main_platform_delegate.h"
#include "content/zygote/zygote_main.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include <charm/libc.h>

namespace content {

int GetSandboxFD() {
  return -1;
}

// --- SandboxHostLinux ------------------------------------------------------

SandboxHostLinux* SandboxHostLinux::GetInstance() {
  static base::NoDestructor<SandboxHostLinux> instance;
  return instance.get();
}

SandboxHostLinux::SandboxHostLinux() = default;

void SandboxHostLinux::Init() {
  // The in-process renderer has no sandbox IPC socket.
  child_socket_ = -1;
  childs_lifeline_fd_ = -1;
  initialized_ = true;
}

namespace responsiveness {

void BrowserUINativeEventObserver::RegisterObserver() {
  // CharmOS does not expose a ui::PlatformEventSource in the browser process;
  // jank monitoring therefore has no native event hook.
}

void BrowserUINativeEventObserver::UnregisterObserver() {}

}  // namespace responsiveness

// --- ZygoteCommunication ---------------------------------------------------

ZygoteCommunication::ZygoteCommunication(ZygoteType type)
    : type_(type),
      pid_(base::kNullProcessHandle),
      sandbox_status_(0),
      have_read_sandbox_status_word_(false),
      init_(false) {}

ZygoteCommunication::~ZygoteCommunication() = default;

void ZygoteCommunication::Init(ZygoteLaunchCallback /*launcher*/) {
  init_ = false;
}

void ZygoteCommunication::EnsureLaunchFinished() {}

void ZygoteCommunication::ReinitializeLogging(uint32_t /*logging_dest*/,
                                              base::PlatformFile /*log_file_fd*/) {}

pid_t ZygoteCommunication::ForkRequest(
    const std::vector<std::string>& /*command_line*/,
    const base::FileHandleMappingVector& /*mapping*/,
    const std::string& /*process_type*/) {
  return static_cast<pid_t>(base::kNullProcessHandle);
}

void ZygoteCommunication::EnsureProcessTerminated(pid_t /*process*/) {}

void ZygoteCommunication::ZygoteChildDied(pid_t /*process*/) {}

base::TerminationStatus ZygoteCommunication::GetTerminationStatus(
    base::ProcessHandle /*handle*/,
    bool /*known_dead*/,
    int* exit_code) {
  if (exit_code) *exit_code = 0;
  return base::TERMINATION_STATUS_STILL_RUNNING;
}

int ZygoteCommunication::GetSandboxStatus() {
  return 0;
}

// --- ZygoteHostImpl --------------------------------------------------------

ZygoteHostImpl::ZygoteHostImpl()
    : use_namespace_sandbox_(false),
      use_suid_sandbox_(false),
      use_suid_sandbox_for_adj_oom_score_(false) {}

ZygoteHostImpl::~ZygoteHostImpl() = default;

ZygoteHostImpl* ZygoteHostImpl::GetInstance() {
  return base::Singleton<ZygoteHostImpl,
                         base::DefaultSingletonTraits<ZygoteHostImpl>>::get();
}

void ZygoteHostImpl::Init(const base::CommandLine& /*cmd_line*/) {}

bool ZygoteHostImpl::IsZygotePid(pid_t /*pid*/) {
  return false;
}

int ZygoteHostImpl::GetRendererSandboxStatus() {
  return 0;
}

ZygoteLaunchCompletionCallback ZygoteHostImpl::LaunchZygote(
    base::CommandLine* /*cmd_line*/,
    base::ScopedFD* /*control_fd*/,
    base::FileHandleMappingVector /*additional_remapped_fds*/) {
  return base::BindOnce(
      []() -> pid_t { return static_cast<pid_t>(base::kNullProcessHandle); });
}

void ZygoteHostImpl::AdjustRendererOOMScore(base::ProcessHandle /*process_handle*/,
                                            int /*score*/) {}

// --- Zygote entry point ----------------------------------------------------

bool ZygoteMain(
    std::vector<std::unique_ptr<ZygoteForkDelegate>> /*fork_delegates*/) {
  return false;
}

// --- RendererMainPlatformDelegate -----------------------------------------

RendererMainPlatformDelegate::RendererMainPlatformDelegate(
    const MainFunctionParams& /*parameters*/) {}

RendererMainPlatformDelegate::~RendererMainPlatformDelegate() = default;

void RendererMainPlatformDelegate::PlatformInitialize() {}

void RendererMainPlatformDelegate::PlatformUninitialize() {}

bool RendererMainPlatformDelegate::EnableSandbox() {
  // There is no seccomp/namespace layer. Process isolation still comes from
  // the native address space and capability table.
  return true;
}

// --- ChildProcessLauncherHelper platform hooks -----------------------------

namespace internal {

std::optional<mojo::NamedPlatformChannel>
ChildProcessLauncherHelper::CreateNamedPlatformChannelOnLauncherThread() {
  return std::nullopt;
}

void ChildProcessLauncherHelper::BeforeLaunchOnClientThread() {}

std::unique_ptr<FileMappedForLaunch>
ChildProcessLauncherHelper::GetFilesToMap() {
  // CharmOS transfers startup objects through LaunchOptions capabilities,
  // but the common launcher still requires a non-null staging container.
  return PosixFileDescriptorInfoImpl::Create();
}

bool ChildProcessLauncherHelper::IsUsingLaunchOptions() {
  return true;
}

bool ChildProcessLauncherHelper::BeforeLaunchOnLauncherThread(
    FileMappedForLaunch& /*files_to_register*/,
    base::LaunchOptions* options) {
  if (!options) return false;
  // PlatformChannel performs the one-shot role allocation and appends the
  // serialized role switch. No descriptor numbers cross this boundary.
  mojo_channel_->PrepareToPassRemoteEndpoint(options, command_line());
  options->environment = delegate_->GetEnvironment();
  options->charmos_capabilities =
      CHARM_GIVE_ROOT | CHARM_GIVE_CWD | CHARM_GIVE_NET | CHARM_GIVE_CONSOLE;
  options->charmos_priority = 8;
  return true;
}

ChildProcessLauncherHelper::Process
ChildProcessLauncherHelper::LaunchProcessOnLauncherThread(
    const base::LaunchOptions* options,
    std::unique_ptr<FileMappedForLaunch> /*files_to_register*/,
    bool* is_synchronous_launch,
    int* launch_result) {
  if (is_synchronous_launch) *is_synchronous_launch = true;
  Process process;
  if (!options) {
    if (launch_result) *launch_result = LAUNCH_RESULT_FAILURE;
    return process;
  }
  process.process = base::LaunchProcess(*command_line(), *options);
  if (!process.process.IsValid())
    LOG(ERROR) << "charmos-child: launch " << GetProcessType()
               << " failed, errno=" << errno;
  if (launch_result) {
    *launch_result = process.process.IsValid() ? LAUNCH_RESULT_SUCCESS
                                               : LAUNCH_RESULT_FAILURE;
  }
  return process;
}

void ChildProcessLauncherHelper::AfterLaunchOnLauncherThread(
    const ChildProcessLauncherHelper::Process& /*process*/,
    const base::LaunchOptions* /*options*/) {
  file_data_.reset();
}

ChildProcessTerminationInfo ChildProcessLauncherHelper::GetTerminationInfo(
    const ChildProcessLauncherHelper::Process& process,
    bool known_dead) {
  ChildProcessTerminationInfo info;
  if (known_dead) {
    info.status = base::GetKnownDeadTerminationStatus(
        process.process.Handle(), &info.exit_code);
  } else {
    info.status = base::GetTerminationStatus(process.process.Handle(),
                                             &info.exit_code);
  }
  return info;
}

bool ChildProcessLauncherHelper::TerminateProcess(
    const base::Process& process,
    int exit_code) {
  return process.Terminate(exit_code, false);
}

void ChildProcessLauncherHelper::ForceNormalProcessTerminationSync(
    ChildProcessLauncherHelper::Process process) {
  base::EnsureProcessTerminated(std::move(process.process));
}

void ChildProcessLauncherHelper::SetProcessPriorityOnLauncherThread(
    base::Process /*process*/,
    base::Process::Priority /*priority*/) {}

}  // namespace internal

}  // namespace content
