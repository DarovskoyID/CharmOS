// Запуск программ (docs/abi/process.md §3): charm_spawn, ожидание, журнал, сигналы.
#include <abi/process.hpp>
#include <abi/signal.hpp>
#include <charm/libc.h>
#include <charm/process.hpp>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal.hpp"

using namespace libc;
namespace ap = abi::process;
namespace r = abi::syscall::rights;

namespace {

// Копия хендла для ребёнка: прав мы не знаем — пробуем от широких к узким.
Handle dup_any(Handle h, const uint32_t* candidates, int n,Status* error) {
  for (int i = 0; i < n; ++i) {
    auto d = charm::handle_duplicate(h, candidates[i]);
    if (d.ok()) return static_cast<Handle>(d.value);
    *error=d.status;
    if(d.status!=Status::AccessDenied) return 0;
  }
  return 0;
}

constexpr uint32_t kBase = r::kDuplicate | r::kTransfer;

// Склеивает строки (с \0) в блок; false — не влезло.
bool pack(char* const* v, char* out, size_t cap,uint32_t max_count, uint32_t* bytes, uint32_t* count, const char* extra) {
  size_t len = 0;
  uint32_t n = 0;
  for (; v != nullptr && v[n] != nullptr; ++n) {
    if(n>=max_count) return false;
    size_t l = strnlen(v[n],cap);
    if (l==cap || l+1>cap-len) return false;
    ++l;
    memcpy(out + len, v[n], l);
    len += l;
  }
  if (extra != nullptr) {
    if(n>=max_count) return false;
    size_t l = strlen(extra) + 1;
    if (len + l > cap) return false;
    memcpy(out + len, extra, l);
    len += l;
    ++n;
  }
  *bytes = static_cast<uint32_t>(len);
  *count = n;
  return true;
}

char g_args[ap::kMaxArgsBytes];
char g_env[ap::kMaxEnvBytes];
Lock g_spawn_lock;

sighandler_t g_handlers[NSIG] = {};
sigset_t g_masks[NSIG] = {};
int g_flags[NSIG] = {};
char g_sig_stack[16 * 1024] __attribute__((aligned(16)));

void dispatch(uint64_t signo, void* info, void* context);

// The kernel has one dispatch entry (SignalSetHandler); sigaction and signal
// both store their per-signal state here. False if the entry cannot be set.
bool ensure_dispatch() {
  static bool installed = false;
  if (installed) return true;
  auto res = charm::syscall3(abi::syscall::Number::SignalSetHandler,
                             reinterpret_cast<uint64_t>(&dispatch),
                             reinterpret_cast<uint64_t>(g_sig_stack + sizeof g_sig_stack),
                             sizeof g_sig_stack);
  if (!res.ok()) {
    errno = errno_from(res.status);
    return false;
  }
  installed = true;
  return true;
}

void dispatch(uint64_t signo, void* info, void* context) {
  sighandler_t h = signo < NSIG ? g_handlers[signo] : SIG_IGN;
  sigset_t blocked = signo < NSIG ? g_masks[signo] : 0;
  sigset_t old_mask = 0;
  const bool apply_mask =
      blocked != 0 && sigprocmask(SIG_BLOCK, &blocked, &old_mask) == 0;
  if (h == SIG_DFL) {
    // Действие по умолчанию (process.md §9): завершить или игнорировать.
    if (signo == SIGINT || signo == SIGQUIT || signo == SIGTERM || signo == SIGSEGV) {
      charm::process_exit(ap::kExitKilled);
    }
  } else if (h != SIG_IGN) {
    if (signo < NSIG && (g_flags[signo] & SA_SIGINFO) != 0) {
      // Translate the kernel's SignalInfo into the libc siginfo_t layout.
      siginfo_t si{};
      si.si_signo = static_cast<int>(signo);
      if (info != nullptr) {
        const auto* ki = static_cast<const abi::signal::SignalInfo*>(info);
        si.si_code = static_cast<int>(ki->code);
        si.si_addr = reinterpret_cast<void*>(ki->addr);
      }
      void (*handler)(int, siginfo_t*, void*) = nullptr;
      static_assert(sizeof handler == sizeof h);
      memcpy(&handler, &h, sizeof handler);
      handler(static_cast<int>(signo), &si, context);
    } else {
      h(static_cast<int>(signo));
    }
  }
  if (apply_mask) (void)sigprocmask(SIG_SETMASK, &old_mask, nullptr);
  charm::syscall0(abi::syscall::Number::SignalReturn);
}

}  // namespace

extern "C" {

int64_t charm_spawn(const char* path, char* const argv[], char* const envp[], const struct charm_spawn_opts* o) {
  if(o && ((o->give&~CHARM_GIVE_ALL) || o->priority>ap::kPriorityMax ||
           o->extra_count>ap::kMaxStartHandlesV2 || (o->extra_count && !o->extra)))
    return fail_errno(EINVAL);
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  Guard g(g_spawn_lock);
  ap::SpawnArgs a{};
  a.executable = charm_fd_handle(fd);
  char pwd[abi::fs::kMaxPath + 8];
  uint32_t give = o != nullptr ? o->give : CHARM_GIVE_ALL;
  snprintf(pwd, sizeof pwd, "PWD=/%s", libc::cwd());
  char* const* env = envp != nullptr ? envp : environ;
  // PWD из окружения заменяется текущим каталогом.
  char* filtered[ap::kMaxEnv + 1];
  uint32_t nf = 0;
  uint32_t env_capacity=ap::kMaxEnv-((give&CHARM_GIVE_CWD) ? 1u : 0u);
  for (uint32_t i = 0; env != nullptr && env[i] != nullptr; ++i) {
    if ((give & CHARM_GIVE_CWD) != 0 && strncmp(env[i], "PWD=", 4) == 0) continue;
    if(nf>=env_capacity) { close(fd); return fail_errno(E2BIG); }
    filtered[nf++] = env[i];
  }
  filtered[nf] = nullptr;
  if (!pack(argv, g_args, sizeof g_args,ap::kMaxArgs, &a.args_bytes, &a.argc, nullptr) ||
      !pack(filtered, g_env, sizeof g_env,ap::kMaxEnv, &a.env_bytes, &a.envc, (give & CHARM_GIVE_CWD) != 0 ? pwd : nullptr)) {
    close(fd);
    return fail_errno(E2BIG);
  }
  a.args = g_args;
  a.env = g_env;
  const char* name = strrchr(path, '/');
  name = name != nullptr ? name + 1 : path;
  a.name = name;
  a.name_len = static_cast<uint32_t>(strnlen(name, ap::kMaxName));

  uint32_t handles[ap::kMaxStartHandlesV2];
  uint32_t roles[ap::kMaxStartHandlesV2];
  uint32_t n = 0;
  Status duplicate_error=Status::Ok;
  StartInfo& si = start_info();
  auto add = [&](Handle src, uint32_t role, const uint32_t* cand, int nc) {
    if (src == 0 || duplicate_error!=Status::Ok) return;
    if(n==ap::kMaxStartHandlesV2) { duplicate_error=Status::NoSpace; return; }
    Status error=Status::AccessDenied;
    Handle d = dup_any(src, cand, nc,&error);
    if (d == 0) { duplicate_error=error; return; }
    handles[n] = d;
    roles[n++] = role;
  };
  const uint32_t rw[] = {kBase | r::kRead | r::kWrite, kBase | r::kRead};
  const uint32_t net[] = {kBase | r::kNetAll, kBase | r::kNetConnect | r::kNetListen | r::kNetResolve,
                          kBase | r::kNetConnect | r::kNetResolve};
  const uint32_t job[] = {kBase | r::kRead | r::kWrite | r::kSpawn, kBase | r::kRead | r::kSpawn};
  const uint32_t logc[] = {kBase | r::kWrite, kBase | r::kRead | r::kWrite};
  if ((give & CHARM_GIVE_ROOT) != 0) add(si.root, ap::kRoleRoot, rw, 2);
  if ((give & CHARM_GIVE_CONSOLE) != 0) add(si.console, ap::kRoleConsole, rw, 2);
  if ((give & CHARM_GIVE_NET) != 0) add(si.net, ap::kRoleNet, net, 3);
  if ((give & CHARM_GIVE_LOG) != 0) add(si.log, ap::kRoleLog, logc, 2);
  if ((give & CHARM_GIVE_JOB) != 0) add(si.job, ap::kRoleJob, job, 2);
  const uint32_t door[] = {kBase | r::kWrite};  // wm.md §1: только писать (и передавать дальше)
  if ((give & CHARM_GIVE_WM) != 0) add(si.wm, ap::kRoleWm, door, 1);
  if ((give & CHARM_GIVE_AUDIO) != 0) add(si.audio, ap::kRoleAudio, door, 1);
  if(duplicate_error!=Status::Ok || (o && o->extra_count>ap::kMaxStartHandlesV2-n)) {
    for(uint32_t i=0;i<n;++i) (void)charm::handle_close(handles[i]);
    close(fd);
    return duplicate_error==Status::Ok ? fail_errno(E2BIG) : fail(duplicate_error);
  }
  for (uint32_t i = 0; o != nullptr && i < o->extra_count; ++i) {
    handles[n] = o->extra[i];
    roles[n++] = o->extra_roles != nullptr ? o->extra_roles[i] : 0;
  }
  a.handle_count = n;
  a.flags = ap::kSpawnStartV2;
  a.handles = handles;
  a.roles = roles;
  a.job = o != nullptr ? o->job : 0;
  a.priority = o != nullptr ? o->priority : 0;
  auto res = charm::process_spawn(&a);
  close(fd);
  if (!res.ok()) {
    // Копии, что мы сделали, остались у нас — закрыть (extra — вызывающего).
    uint32_t own = n - (o != nullptr ? o->extra_count : 0);
    for (uint32_t i = 0; i < own; ++i) (void)charm::handle_close(handles[i]);
    if (res.status == Status::InvalidArgs) return fail_errno(ENOEXEC);
    return fail(res.status);
  }
  return static_cast<int64_t>(res.value);
}

int charm_wait(uint32_t proc, int64_t* code) {
  Status status;
  do { status=charm::ProcessView(proc).wait(charm::kForever,code); } while(status==Status::Interrupted);
  return status==Status::Ok ? 0 : fail(status);
}

int charm_kill(uint32_t proc) {
  Status s = charm::process_kill(proc);
  return s == Status::Ok ? 0 : fail(s);
}

int charm_log_write(uint32_t level, const char* text) {
  Handle log = start_info().log;
  if (log == 0) return fail_errno(ENOSYS);
  // Сообщение logd (console.md §2): uint32 level, затем текст до 1020 байт.
  char msg[1024];
  memcpy(msg, &level, 4);
  size_t n = strnlen(text, sizeof msg - 4);
  memcpy(msg + 4, text, n);
  Status s = charm::channel_write(log, msg, 4 + n, nullptr, 0);
  return s == Status::Ok ? 0 : fail(s);
}

// --- signal.h --------------------------------------------------------------------------------

sighandler_t signal(int signo, sighandler_t h) {
  if (signo <= 0 || signo >= NSIG) {
    errno = EINVAL;
    return SIG_ERR;
  }
  if (!ensure_dispatch()) return SIG_ERR;
  sighandler_t old = g_handlers[signo];
  g_handlers[signo] = h;
  g_masks[signo] = 0;
  g_flags[signo] = 0;
  // Без обработчика ядро бы завершило процесс; SIG_DFL эмулируем выходом.
  return old;
}

int sigemptyset(sigset_t* s) {
  if (!s) return fail_errno(EFAULT);
  *s = 0;
  return 0;
}
int sigfillset(sigset_t* s) {
  if (!s) return fail_errno(EFAULT);
  *s = (1ul << NSIG) - 2;
  return 0;
}

int sigaddset(sigset_t* s, int signo) {
  if (!s) return fail_errno(EFAULT);
  if (signo <= 0 || signo >= NSIG) return fail_errno(EINVAL);
  *s |= 1ul << signo;
  return 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* old) {
  if (set && how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)
    return fail_errno(EINVAL);
  uint64_t block = 0, unblock = 0;
  if (set != nullptr) {
    if (how == SIG_BLOCK) block = *set;
    else if (how == SIG_UNBLOCK) unblock = *set;
    else {
      block = *set;
      unblock = ~*set;
    }
  }
  auto res = charm::syscall2(abi::syscall::Number::SignalMask, block, unblock);
  if (!res.ok()) return fail(res.status);
  if (old != nullptr) *old = res.value;
  return 0;
}
int sigaltstack(const stack_t* ss, stack_t* old_ss) {
  // Querying the absent alternate stack is supported. Installing a stack
  // cannot succeed until the kernel implements alternate-stack delivery.
  if (ss && ss->ss_flags!=SS_DISABLE) {
    if (ss->ss_flags!=0) return fail_errno(EINVAL);
    return fail_errno(ENOTSUP);
  }
  if (old_ss != nullptr) {
    old_ss->ss_sp = nullptr;
    old_ss->ss_flags = SS_DISABLE;
    old_ss->ss_size = 0;
  }
  return 0;
}
int sigaction(int signo, const struct sigaction* act, struct sigaction* old) {
  if (signo <= 0 || signo >= NSIG) return fail_errno(EINVAL);
  if (!ensure_dispatch()) return -1;
  if (old != nullptr) {
    memset(old, 0, sizeof *old);
    old->sa_handler = g_handlers[signo];
    old->sa_mask = g_masks[signo];
    old->sa_flags = g_flags[signo];
  }
  if (act != nullptr) {
    g_handlers[signo] = act->sa_handler;
    g_masks[signo] = act->sa_mask;
    g_flags[signo] = act->sa_flags;
  }
  return 0;
}
int pthread_sigmask(int how, const sigset_t* set, sigset_t* old) {
  int saved_errno = errno;
  int result = sigprocmask(how, set, old);
  int error = result < 0 ? errno : 0;
  errno = saved_errno;
  return error;
}

unsigned alarm(unsigned seconds) {
  uint64_t at = seconds == 0 ? 0 : charm::now_ns() + uint64_t{seconds} * 1'000'000'000;
  (void)charm::syscall1(abi::syscall::Number::SignalAlarm, at);
  return 0;
}

}  // extern "C"
