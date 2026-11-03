#include "base/process/launch.h"
#include "base/check.h"
#include "base/logging.h"
#include <charm/libc.h>
#include <charm/handle.hpp>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

namespace base {
namespace {
bool ValidString(const std::string& value) { return value.find('\0')==std::string::npos; }
bool EnvironmentFor(const LaunchOptions& options,EnvironmentMap* environment) {
  if(!options.clear_environment) {
    for(char** entry=environ;entry && *entry;++entry) {
      std::string item(*entry);
      size_t separator=item.find('=');
      if(separator!=std::string::npos && separator) (*environment)[item.substr(0,separator)]=item.substr(separator+1);
    }
  }
  for(const auto& [key,value]:options.environment) {
    if(key.empty() || key.find('=')!=std::string::npos || !ValidString(key) || !ValidString(value)) {
      errno=EINVAL; return false;
    }
    if(value.empty()) environment->erase(key);
    else (*environment)[key]=value;
  }
  if(!options.current_directory.empty()) {
    const auto& path=options.current_directory.value();
    if(!options.current_directory.IsAbsolute() || !ValidString(path)) { errno=EINVAL; return false; }
    struct stat information{};
    if(stat(path.c_str(),&information)) return false;
    if(!S_ISDIR(information.st_mode)) { errno=ENOTDIR; return false; }
    (*environment)["PWD"]=path;
  }
  return true;
}
FilePath FindExecutable(const std::string& program,const LaunchOptions& options,const EnvironmentMap& environment) {
  FilePath directory=options.current_directory;
  if(directory.empty()) {
    char working[PATH_MAX];
    if(!getcwd(working,sizeof working)) return {};
    directory=FilePath(working);
  }
  FilePath path(program);
  if(path.IsAbsolute()) return path;
  if(program.find('/')!=std::string::npos) return directory.Append(path);
  auto selected=environment.find("PATH");
  std::string search=selected==environment.end() ? "/bin" : selected->second;
  size_t offset=0;
  for(;;) {
    size_t separator=search.find(':',offset);
    auto component=search.substr(offset,separator==std::string::npos ? std::string::npos : separator-offset);
    FilePath parent=component.empty() ? directory : FilePath(component);
    if(!parent.IsAbsolute()) parent=directory.Append(parent);
    auto candidate=parent.Append(path);
    struct stat information{};
    if(stat(candidate.value().c_str(),&information)==0 && S_ISREG(information.st_mode)) return candidate;
    if(separator==std::string::npos) break;
    offset=separator+1;
  }
  errno=ENOENT;
  return {};
}
bool UnsupportedOutput() { errno=ENOTSUP; return false; }
}
void CheckPThreadStackMinIsSafe() { CHECK(PTHREAD_STACK_MIN_CONST>=PTHREAD_STACK_MIN); }
Process LaunchProcess(const CommandLine& command,const LaunchOptions& options) {
  return LaunchProcess(command.argv(),options);
}
Process LaunchProcess(const std::vector<std::string>& arguments,const LaunchOptions& options) {
  if(arguments.empty() || arguments[0].empty()) {
    errno=EINVAL; return Process();
  }
  if(arguments.size()>abi::process::kMaxArgs) { errno=E2BIG; return Process(); }
  if(options.pre_exec_delegate || options.new_process_group ||
     (options.maximize_rlimits && !options.maximize_rlimits->empty()) || !options.fds_to_remap.empty()) {
    errno=ENOTSUP; return Process();
  }
  if((options.charmos_capabilities&~CHARM_GIVE_ALL) || options.charmos_priority>abi::process::kPriorityMax ||
     options.charmos_handles.size()>abi::process::kMaxStartHandlesV2) { errno=EINVAL; return Process(); }
  std::vector<char*> argv;
  for(const auto& argument:arguments) {
    if(!ValidString(argument)) { errno=EINVAL; return Process(); }
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);
  EnvironmentMap environment;
  if(!EnvironmentFor(options,&environment)) return Process();
  if(environment.size()>abi::process::kMaxEnv) { errno=E2BIG; return Process(); }
  const std::string& program=options.real_path.empty() ? arguments[0] : options.real_path.value();
  if(!ValidString(program)) { errno=EINVAL; return Process(); }
  auto executable=FindExecutable(program,options,environment);
  if(executable.empty()) return Process();
  std::vector<std::string> env_strings;
  for(const auto& [key,value]:environment) env_strings.push_back(key+"="+value);
  std::vector<char*> envp;
  for(auto& value:env_strings) envp.push_back(value.data());
  envp.push_back(nullptr);

  // Duplicate first; the caller's original capabilities are never consumed,
  // including a failure after the kernel started preparing a child.
  std::vector<charm::UniqueHandle> owners;
  std::vector<uint32_t> handles,roles;
  for(const auto& transfer:options.charmos_handles) {
    if(!transfer.handle || !(transfer.rights&charm::rights::kTransfer)) { errno=EINVAL; return Process(); }
    auto copy=charm::handle_duplicate(transfer.handle,transfer.rights);
    if(!copy.ok()) { errno=copy.status==charm::Status::NoMemory ? ENOMEM : EACCES; return Process(); }
    owners.push_back(charm::UniqueHandle::adopt(static_cast<charm::Handle>(copy.value)));
    handles.push_back(static_cast<uint32_t>(copy.value)); roles.push_back(transfer.role);
  }
  charm_spawn_opts spawn{};
  spawn.give=options.charmos_capabilities;
  if(!options.current_directory.empty()) spawn.give&=~CHARM_GIVE_CWD;
  spawn.job=options.charmos_job;
  spawn.priority=options.charmos_priority;
  spawn.extra_count=static_cast<uint32_t>(handles.size());
  spawn.extra=handles.data(); spawn.extra_roles=roles.data();
  int64_t handle=charm_spawn(executable.value().c_str(),argv.data(),envp.data(),&spawn);
  if(handle<0) return Process();
  for(auto& owner:owners) (void)owner.release();
  Process process(static_cast<ProcessHandle>(handle));
  if(options.wait && !process.WaitForExit(nullptr)) {
    (void)process.Terminate(-1,false);
    return Process();
  }
  return process;
}
bool GetAppOutput(const CommandLine&,std::string*) { return UnsupportedOutput(); }
bool GetAppOutputAndError(const CommandLine&,std::string*) { return UnsupportedOutput(); }
bool GetAppOutputWithExitCode(const CommandLine&,std::string*,int*) { return UnsupportedOutput(); }
bool GetAppOutput(const std::vector<std::string>&,std::string*) { return UnsupportedOutput(); }
bool GetAppOutputAndError(const std::vector<std::string>&,std::string*) { return UnsupportedOutput(); }
bool GetAppOutputWithExitCode(const std::vector<std::string>&,std::string*,int*) { return UnsupportedOutput(); }
void RaiseProcessToHighPriority() { DLOG(WARNING)<<"Native whole-process priority changes are unavailable"; }
}  // namespace base
