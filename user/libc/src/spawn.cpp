// Запуск программ (docs/abi/process.md §3): charm_spawn, ожидание, журнал, сигналы.
#include <abi/process.hpp>
#include <charm/libc.h>
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
Handle dup_any(Handle h, const uint32_t* candidates, int n) {
  for (int i = 0; i < n; ++i) {
    auto d = charm::handle_duplicate(h, candidates[i]);
    if (d.ok()) return static_cast<Handle>(d.value);
  }
  return 0;
}

constexpr uint32_t kBase = r::kDuplicate | r::kTransfer;

// Склеивает строки (с \0) в блок; false — не влезло.
bool pack(char* const* v, char* out, size_t cap, uint32_t* bytes, uint32_t* count, const char* extra) {
  size_t len = 0;
  uint32_t n = 0;
  for (; v != nullptr && v[n] != nullptr; ++n) {
    size_t l = strlen(v[n]) + 1;
    if (len + l > cap) return false;
    memcpy(out + len, v[n], l);
    len += l;
  }
  if (extra != nullptr) {
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
char g_sig_stack[16 * 1024] __attribute__((aligned(16)));

void dispatch(uint64_t signo, void*, void*) {
  sighandler_t h = signo < NSIG ? g_handlers[signo] : SIG_IGN;
  if (h == SIG_DFL) {
    // Действие по умолчанию (process.md §9): завершить или игнорировать.
    if (signo == SIGINT || signo == SIGQUIT || signo == SIGTERM || signo == SIGSEGV) {
      charm::process_exit(ap::kExitKilled);
    }
  } else if (h != SIG_IGN) {
    h(static_cast<int>(signo));
  }
  charm::syscall0(abi::syscall::Number::SignalReturn);
}

}  // namespace

extern "C" {

int64_t charm_spawn(const char* path, char* const argv[], char* const envp[], const struct charm_spawn_opts* o) {
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
  for (uint32_t i = 0; env != nullptr && env[i] != nullptr && nf < ap::kMaxEnv - 1; ++i) {
    if ((give & CHARM_GIVE_CWD) != 0 && strncmp(env[i], "PWD=", 4) == 0) continue;
    filtered[nf++] = env[i];
  }
  filtered[nf] = nullptr;
  if (!pack(argv, g_args, sizeof g_args, &a.args_bytes, &a.argc, nullptr) ||
      !pack(filtered, g_env, sizeof g_env, &a.env_bytes, &a.envc, (give & CHARM_GIVE_CWD) != 0 ? pwd : nullptr)) {
    close(fd);
    return fail_errno(E2BIG);
  }
  a.args = g_args;
  a.env = g_env;
  const char* name = strrchr(path, '/');
  name = name != nullptr ? name + 1 : path;
  a.name = name;
  a.name_len = static_cast<uint32_t>(strnlen(name, ap::kMaxName));

  uint32_t handles[ap::kMaxStartHandles];
  uint32_t roles[ap::kMaxStartHandles];
  uint32_t n = 0;
  StartInfo& si = start_info();
  auto add = [&](Handle src, uint32_t role, const uint32_t* cand, int nc) {
    if (src == 0 || n == ap::kMaxStartHandles) return;
    Handle d = dup_any(src, cand, nc);
    if (d == 0) return;
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
  for (uint32_t i = 0; o != nullptr && i < o->extra_count && n < ap::kMaxStartHandles; ++i) {
    handles[n] = o->extra[i];
    roles[n++] = o->extra_roles != nullptr ? o->extra_roles[i] : 0;
  }
  a.handle_count = n;
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
  auto port = charm::port_create();
  if (!port.ok()) return fail(port.status);
  Handle p = static_cast<Handle>(port.value);
  Status s = charm::object_bind(p, proc, 1);
  charm::PortPacket pkt{};
  while (s == Status::Ok || s == Status::Interrupted) {
    s = charm::port_wait(p, &pkt, charm::kForever);
    if (s == Status::Ok) break;
  }
  (void)charm::handle_close(p);
  if (s != Status::Ok) return fail(s);
  if (code != nullptr) *code = static_cast<int64_t>(pkt.data0);
  return 0;
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
  static bool installed = false;
  if (!installed) {
    auto res = charm::syscall3(abi::syscall::Number::SignalSetHandler, reinterpret_cast<uint64_t>(&dispatch),
                               reinterpret_cast<uint64_t>(g_sig_stack + sizeof g_sig_stack), sizeof g_sig_stack);
    if (!res.ok()) {
      errno = errno_from(res.status);
      return SIG_ERR;
    }
    installed = true;
  }
  sighandler_t old = g_handlers[signo];
  g_handlers[signo] = h;
  // Без обработчика ядро бы завершило процесс; SIG_DFL эмулируем выходом.
  return old;
}

int sigemptyset(sigset_t* s) {
  *s = 0;
  return 0;
}

int sigaddset(sigset_t* s, int signo) {
  *s |= 1ul << signo;
  return 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* old) {
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

unsigned alarm(unsigned seconds) {
  uint64_t at = seconds == 0 ? 0 : charm::now_ns() + uint64_t{seconds} * 1'000'000'000;
  (void)charm::syscall1(abi::syscall::Number::SignalAlarm, at);
  return 0;
}

}  // extern "C"
