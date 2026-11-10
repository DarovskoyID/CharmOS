// CharmOS content-process architecture.
//
// CharmOS starts programs explicitly through charm_spawn and has no fork/exec
// or setuid/namespace sandbox. Content Shell therefore runs the renderer
// in-process (--single-process). The symbols below are the Linux zygote and
// sandbox entry points that upstream content still references unconditionally;
// they report "no zygote / no sandbox" instead of pretending a process was
// created. This keeps failures explicit while allowing the browser to run.
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/singleton.h"
#include "base/no_destructor.h"
#include "base/process/kill.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "content/browser/child_process_launcher_helper.h"
#include "content/browser/sandbox_host_linux.h"
#include "content/browser/scheduler/responsiveness/native_event_observer.h"
#include "content/browser/zygote_host/zygote_host_impl_linux.h"
#include "content/common/zygote/zygote_communication_linux.h"
#include "content/public/browser/child_process_termination_info.h"
#include "content/public/common/zygote/sandbox_support_linux.h"
#include "content/public/common/zygote/zygote_fork_delegate_linux.h"
#include "content/renderer/renderer_main_platform_delegate.h"
#include "content/zygote/zygote_main.h"

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
  // No seccomp/namespace layer exists; the renderer is in-process.
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
  return nullptr;
}

bool ChildProcessLauncherHelper::IsUsingLaunchOptions() {
  return true;
}

bool ChildProcessLauncherHelper::BeforeLaunchOnLauncherThread(
    FileMappedForLaunch& /*files_to_register*/,
    base::LaunchOptions* /*options*/) {
  return false;
}

ChildProcessLauncherHelper::Process
ChildProcessLauncherHelper::LaunchProcessOnLauncherThread(
    const base::LaunchOptions* /*options*/,
    std::unique_ptr<FileMappedForLaunch> /*files_to_register*/,
    bool* is_synchronous_launch,
    int* launch_result) {
  if (is_synchronous_launch) *is_synchronous_launch = true;
  if (launch_result) *launch_result = 0;
  return Process();
}

void ChildProcessLauncherHelper::AfterLaunchOnLauncherThread(
    const ChildProcessLauncherHelper::Process& /*process*/,
    const base::LaunchOptions* /*options*/) {}

ChildProcessTerminationInfo ChildProcessLauncherHelper::GetTerminationInfo(
    const ChildProcessLauncherHelper::Process& /*process*/,
    bool /*known_dead*/) {
  return ChildProcessTerminationInfo();
}

bool ChildProcessLauncherHelper::TerminateProcess(
    const base::Process& /*process*/,
    int /*exit_code*/) {
  return false;
}

void ChildProcessLauncherHelper::ForceNormalProcessTerminationSync(
    ChildProcessLauncherHelper::Process /*process*/) {}

void ChildProcessLauncherHelper::SetProcessPriorityOnLauncherThread(
    base::Process /*process*/,
    base::Process::Priority /*priority*/) {}

}  // namespace internal

}  // namespace content
