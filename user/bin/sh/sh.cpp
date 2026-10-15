// Шелл (M8). Ввод с консоли посимвольно (эхо и забой — сам), команда —
// /bin/<имя> в новом Job на переднем плане консоли: Ctrl-C завершает её,
// а не шелл. Встроенные: cd, pwd, export, env, svc, help, exit.
#include <abi/process.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

void wait_job(charm::Handle p, charm::Handle job, const char* name);
bool run_app(int argc, char** argv);

using charm::Handle;
using charm::Status;

constexpr int kMaxArgs = 32;
Handle g_job = 0;      // Job шелла (передний план, когда команда не идёт)
Handle g_control = 0;  // канал управления init
Handle g_pkgd = 0;     // канал запросов службы пакетов (pkg.md §3)

Handle dup_job(Handle job) {
  if (job == 0) return 0;
  auto d = charm::handle_duplicate(job, charm::rights::kDuplicate | charm::rights::kTransfer | charm::rights::kRead |
                                            charm::rights::kWrite | charm::rights::kSpawn);
  return d.ok() ? static_cast<Handle>(d.value) : 0;
}

// Запрос pkgd; job — передаётся (перемещается). Ответ: статус, текст, хендл.
Status pkgd_request(const char* text, Handle job, char* out, size_t cap, Handle* proc) {
  if (g_pkgd == 0) {
    snprintf(out, cap, "no package service");
    return Status::NotSupported;
  }
  Status s = charm::channel_write(g_pkgd, text, strlen(text), job != 0 ? &job : nullptr, job != 0 ? 1 : 0);
  if (s != Status::Ok) return s;
  char buf[1024];
  Handle hs[1];
  charm::SysResult r;
  do {
    r = charm::channel_read(g_pkgd, buf, sizeof buf - 1, hs, 1, charm::kForever);
  } while (r.status == Status::Interrupted);
  if (!r.ok()) return r.status;
  uint32_t len = static_cast<uint32_t>(r.value >> 32);
  uint32_t nh = static_cast<uint32_t>(r.value);
  buf[len] = '\0';
  uint32_t st = 0;
  memcpy(&st, buf, 4);
  snprintf(out, cap, "%s", len >= 4 ? buf + 4 : "");
  if (proc != nullptr) *proc = nh == 1 ? hs[0] : 0;
  return static_cast<Status>(st);
}

// Читает строку с эхом; false — конец ввода.
bool read_line(char* buf, size_t cap) {
  size_t len = 0;
  for (;;) {
    char c;
    ssize_t n = read(0, &c, 1);
    if (n < 0 && errno == EINTR) {  // Ctrl-C на пустом шелле
      write(1, "^C\n", 3);
      len = 0;
      buf[0] = '\0';
      return true;
    }
    if (n <= 0) return false;
    if (c == '\r' || c == '\n') {
      write(1, "\n", 1);
      buf[len] = '\0';
      return true;
    }
    if (c == 0x7f || c == '\b') {
      if (len > 0) {
        --len;
        write(1, "\b \b", 3);
      }
      continue;
    }
    if (c == 0x04 && len == 0) return false;  // Ctrl-D
    if (static_cast<unsigned char>(c) < 0x20 || len + 1 >= cap) continue;
    buf[len++] = c;
    write(1, &c, 1);
  }
}

int split(char* line, char** argv) {
  int n = 0;
  char* p = line;
  while (*p != '\0' && n < kMaxArgs) {
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') break;
    if (*p == '"') {
      argv[n++] = ++p;
      while (*p != '\0' && *p != '"') ++p;
    } else {
      argv[n++] = p;
      while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
    }
    if (*p != '\0') *p++ = '\0';
  }
  argv[n] = nullptr;
  return n;
}

void svc(int argc, char** argv) {
  if (g_control == 0) {
    printf("svc: no init control channel\n");
    return;
  }
  char msg[128];
  if (argc >= 2 && strcmp(argv[1], "list") == 0) {
    snprintf(msg, sizeof msg, "list");
  } else if (argc >= 3 && strcmp(argv[1], "kill") == 0) {
    snprintf(msg, sizeof msg, "kill %s", argv[2]);
  } else {
    printf("usage: svc list | svc kill <name>\n");
    return;
  }
  if (charm::channel_write(g_control, msg, strlen(msg), nullptr, 0) != Status::Ok) return;
  char out[1024];
  Handle none[1];
  charm::SysResult r;
  do {
    r = charm::channel_read(g_control, out, sizeof out - 1, none, 1, charm::kForever);
  } while (r.status == Status::Interrupted);
  if (!r.ok()) return;
  uint32_t len = static_cast<uint32_t>(r.value >> 32);
  out[len] = '\0';
  fputs(out, stdout);
}

void run(int argc, char** argv) {
  char path[256];
  if (strchr(argv[0], '/') != nullptr) {
    snprintf(path, sizeof path, "%s", argv[0]);
  } else {
    snprintf(path, sizeof path, "/bin/%s", argv[0]);
  }
  Handle job = 0;
  if (g_job != 0) {
    auto j = charm::job_create(g_job);
    if (j.ok()) job = static_cast<Handle>(j.value);
  }
  charm_spawn_opts o{};
  o.give = CHARM_GIVE_ROOT | CHARM_GIVE_CONSOLE | CHARM_GIVE_CWD;
  o.job = job;
  fflush(stdout);
  int64_t p = charm_spawn(path, argv, nullptr, &o);
  if (p < 0) {
    int err = errno;
    if (job != 0) (void)charm::handle_close(job);
    if (err == ENOENT && strchr(argv[0], '/') == nullptr && run_app(argc, argv)) return;
    printf("sh: %s: %s\n", argv[0], err == ENOENT ? "command not found" : strerror(err));
    return;
  }
  wait_job(static_cast<Handle>(p), job, argv[0]);
}

// Программа, установленная через pkgd (не из /bin).
bool run_app(int argc, char** argv) {
  char req[512];
  size_t used = static_cast<size_t>(snprintf(req, sizeof req, "run"));
  for (int i = 0; i < argc; ++i) used += static_cast<size_t>(snprintf(req + used, sizeof req - used, " %s", argv[i]));
  Handle job = 0;
  if (g_job != 0) {
    auto j = charm::job_create(g_job);
    if (j.ok()) job = static_cast<Handle>(j.value);
  }
  char text[512];
  Handle proc = 0;
  Status s = pkgd_request(req, dup_job(job), text, sizeof text, &proc);
  if (s == Status::NotFound) {
    if (job != 0) (void)charm::handle_close(job);
    return false;
  }
  if (s != Status::Ok || proc == 0) {
    printf("sh: %s\n", text);
    if (job != 0) (void)charm::handle_close(job);
    return true;
  }
  wait_job(proc, job, argv[0]);
  return true;
}

void wait_job(Handle p, Handle job, const char* name) {
  Handle con = charm_console();
  if (job != 0 && con != 0) (void)charm::console_set_foreground(con, job);
  int64_t code = 0;
  (void)charm_wait(p, &code);
  if (g_job != 0 && con != 0) (void)charm::console_set_foreground(con, g_job);
  (void)charm::handle_close(p);
  if (job != 0) (void)charm::handle_close(job);
  if (code == abi::process::kExitKilled) printf("sh: %s: killed\n", name);
}

}  // namespace

extern "C" int main(int, char**) {
  g_job = charm_job();
  g_control = charm_extra_handle(0);
  g_pkgd = charm_extra_handle(1);
  signal(SIGINT, SIG_IGN);  // Ctrl-C — команде на переднем плане, не шеллу
  signal(SIGQUIT, SIG_IGN);
  printf("CharmOS shell. Type 'help'.\n");
  char line[512];
  char* argv[kMaxArgs + 1];
  for (;;) {
    char cwd[256];
    printf("charm:%s$ ", getcwd(cwd, sizeof cwd) != nullptr ? cwd : "?");
    fflush(stdout);
    if (!read_line(line, sizeof line)) break;
    int argc = split(line, argv);
    if (argc == 0) continue;
    if (strcmp(argv[0], "exit") == 0) break;
    if (strcmp(argv[0], "cd") == 0) {
      if (chdir(argc > 1 ? argv[1] : "/") != 0) printf("cd: %s: %s\n", argc > 1 ? argv[1] : "/", strerror(errno));
    } else if (strcmp(argv[0], "pwd") == 0) {
      printf("%s\n", getcwd(cwd, sizeof cwd));
    } else if (strcmp(argv[0], "export") == 0) {
      for (int i = 1; i < argc; ++i) {
        char* eq = strchr(argv[i], '=');
        if (eq == nullptr) continue;
        *eq = '\0';
        setenv(argv[i], eq + 1, 1);
      }
    } else if (strcmp(argv[0], "env") == 0) {
      for (char** e = environ; e != nullptr && *e != nullptr; ++e) printf("%s\n", *e);
    } else if (strcmp(argv[0], "svc") == 0) {
      svc(argc, argv);
    } else if ((strcmp(argv[0], "install") == 0 || strcmp(argv[0], "open") == 0) && argc == 2) {
      // Путь — абсолютный от корня (pkgd открывает от своего корня).
      char abs[512];
      bool url = strncmp(argv[1], "http://", 7) == 0;
      if (url || argv[1][0] == '/') {
        snprintf(abs, sizeof abs, "%s", argv[1]);
      } else {
        snprintf(abs, sizeof abs, "%s/%s", getcwd(cwd, sizeof cwd), argv[1]);
      }
      char req[600], text[512];
      snprintf(req, sizeof req, "%s %s", url ? "install-url" : "install", abs);
      fflush(stdout);
      Status s = pkgd_request(req, dup_job(g_job), text, sizeof text, nullptr);
      printf("%s: %s\n", s == Status::Ok ? "pkg" : "pkg: error", text);
    } else if (strcmp(argv[0], "uninstall") == 0 && argc == 2) {
      char req[300], text[512];
      snprintf(req, sizeof req, "remove %s", argv[1]);
      fflush(stdout);
      Status s = pkgd_request(req, dup_job(g_job), text, sizeof text, nullptr);
      printf("%s: %s\n", s == Status::Ok ? "pkg" : "pkg: error", text);
    } else if (strcmp(argv[0], "apps") == 0) {
      char text[1024];
      (void)pkgd_request("list", 0, text, sizeof text, nullptr);
      fputs(text, stdout);
    } else if (strcmp(argv[0], "help") == 0) {
      printf("builtins: cd pwd export env svc install apps uninstall exit help\ncommands: /bin (ls cat echo mkdir rm sleep), "
             "installed programs by name\n");
    } else {
      run(argc, argv);
    }
  }
  printf("sh: bye\n");
  return 0;
}
