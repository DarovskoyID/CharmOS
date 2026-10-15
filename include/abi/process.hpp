#pragma once
// Запуск процессов, Job, лимиты (syscall ABI v1.5). Нормативный документ —
// docs/abi/process.md; здесь — раскладки структур со static_assert.

#include <stdint.h>

namespace abi::process {

// §2: стартовое сообщение.
inline constexpr char kStartMagic[4] = {'s', 't', 'r', 't'};
inline constexpr uint32_t kMaxArgs = 64;
inline constexpr uint32_t kMaxArgsBytes = 4096;
inline constexpr uint32_t kMaxEnv = 256;
inline constexpr uint32_t kMaxEnvBytes = 8192;
inline constexpr uint32_t kMaxStartHandles = 8;
inline constexpr uint32_t kMaxName = 31;

struct StartHeader {
  char magic[4];
  uint32_t argc;
  uint32_t args_bytes;
  uint32_t handle_count;
  uint32_t roles[kMaxStartHandles];
  uint32_t envc;
  uint32_t env_bytes;
};
static_assert(sizeof(StartHeader) == 56);
static_assert(__builtin_offsetof(StartHeader, roles) == 16);
static_assert(__builtin_offsetof(StartHeader, envc) == 48);
static_assert(__builtin_offsetof(StartHeader, env_bytes) == 52);

// §2.1: роли хендлов.
inline constexpr uint32_t kRoleNone = 0;
inline constexpr uint32_t kRoleRoot = 1;
inline constexpr uint32_t kRoleCwd = 2;
inline constexpr uint32_t kRoleConsole = 3;
inline constexpr uint32_t kRoleNet = 4;
inline constexpr uint32_t kRoleLog = 5;
inline constexpr uint32_t kRoleJob = 6;
inline constexpr uint32_t kRoleSystem = 7;  // v1.6: закрытая область (docs/abi/pkg.md §1)

// §3.1
struct SpawnArgs {
  uint32_t executable;
  uint32_t handle_count;
  const uint32_t* handles;
  const uint32_t* roles;
  const char* args;
  uint32_t args_bytes;
  uint32_t argc;
  const char* name;
  uint32_t name_len;
  uint32_t flags;
  const char* env;
  uint32_t env_bytes;
  uint32_t envc;
  uint32_t job;
  uint32_t priority;
  uint64_t reserved[2];
};
static_assert(sizeof(SpawnArgs) == 96);
static_assert(__builtin_offsetof(SpawnArgs, handles) == 8);
static_assert(__builtin_offsetof(SpawnArgs, roles) == 16);
static_assert(__builtin_offsetof(SpawnArgs, args) == 24);
static_assert(__builtin_offsetof(SpawnArgs, args_bytes) == 32);
static_assert(__builtin_offsetof(SpawnArgs, argc) == 36);
static_assert(__builtin_offsetof(SpawnArgs, name) == 40);
static_assert(__builtin_offsetof(SpawnArgs, name_len) == 48);
static_assert(__builtin_offsetof(SpawnArgs, flags) == 52);
static_assert(__builtin_offsetof(SpawnArgs, env) == 56);
static_assert(__builtin_offsetof(SpawnArgs, env_bytes) == 64);
static_assert(__builtin_offsetof(SpawnArgs, envc) == 68);
static_assert(__builtin_offsetof(SpawnArgs, job) == 72);
static_assert(__builtin_offsetof(SpawnArgs, priority) == 76);
static_assert(__builtin_offsetof(SpawnArgs, reserved) == 80);

// §3.2
struct ProcessInfo {
  uint64_t pid;
  uint32_t state;
  uint32_t reserved;
  int64_t exit_code;
  uint64_t reserved2;
};
static_assert(sizeof(ProcessInfo) == 32);
static_assert(__builtin_offsetof(ProcessInfo, state) == 8);
static_assert(__builtin_offsetof(ProcessInfo, exit_code) == 16);
inline constexpr uint32_t kProcessRunning = 1;
inline constexpr uint32_t kProcessExited = 2;
inline constexpr int64_t kExitKilled = -1000;

// §5
struct JobInfo {
  uint32_t processes;
  uint32_t threads;
  uint64_t memory_pages;
  uint64_t cpu_ns;
  uint64_t reserved;
};
static_assert(sizeof(JobInfo) == 32);
static_assert(__builtin_offsetof(JobInfo, memory_pages) == 8);
static_assert(__builtin_offsetof(JobInfo, cpu_ns) == 16);

// §7
struct JobLimits {
  uint64_t memory_pages;
  uint32_t processes;
  uint32_t threads;
  uint32_t handles_per_process;
  uint32_t max_priority;
  uint64_t cpu_ns;
  uint64_t reserved[2];
};
static_assert(sizeof(JobLimits) == 48);
static_assert(__builtin_offsetof(JobLimits, processes) == 8);
static_assert(__builtin_offsetof(JobLimits, threads) == 12);
static_assert(__builtin_offsetof(JobLimits, handles_per_process) == 16);
static_assert(__builtin_offsetof(JobLimits, max_priority) == 20);
static_assert(__builtin_offsetof(JobLimits, cpu_ns) == 24);
static_assert(__builtin_offsetof(JobLimits, reserved) == 32);

// §8
inline constexpr uint32_t kPriorityMin = 1;
inline constexpr uint32_t kPriorityMax = 31;
inline constexpr uint32_t kPriorityDefault = 16;

// §9
inline constexpr uint32_t kSigInterrupt = 1;
inline constexpr uint32_t kSigQuit = 2;
inline constexpr uint32_t kSigTerminate = 3;
inline constexpr uint32_t kSigChild = 4;
inline constexpr uint32_t kSigFault = 5;
inline constexpr uint32_t kSigAlarm = 6;
inline constexpr uint32_t kSigUser1 = 7;
inline constexpr uint32_t kSigUser2 = 8;
inline constexpr uint32_t kSigMax = 8;

}  // namespace abi::process
