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

using charm::Handle;
using charm::Status;

constexpr int kMaxArgs = 32;
Handle g_job = 0;      // Job шелла (передний план, когда команда не идёт)
Handle g_control = 0;  // канал управления init

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
  (void)argc;
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
    printf("sh: %s: %s\n", argv[0], errno == ENOENT ? "command not found" : strerror(errno));
    if (job != 0) (void)charm::handle_close(job);
    return;
  }
  Handle con = charm_console();
  if (job != 0 && con != 0) (void)charm::console_set_foreground(con, job);
  int64_t code = 0;
  (void)charm_wait(static_cast<Handle>(p), &code);
  if (g_job != 0 && con != 0) (void)charm::console_set_foreground(con, g_job);
  (void)charm::handle_close(static_cast<Handle>(p));
  if (job != 0) (void)charm::handle_close(job);
  if (code == abi::process::kExitKilled) printf("sh: %s: killed\n", argv[0]);
}

}  // namespace

extern "C" int main(int, char**) {
  g_job = charm_job();
  g_control = charm_extra_handle(0);
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
    } else if (strcmp(argv[0], "help") == 0) {
      printf("builtins: cd pwd export env svc exit help\ncommands: /bin (ls cat echo mkdir rm sleep)\n");
    } else {
      run(argc, argv);
    }
  }
  printf("sh: bye\n");
  return 0;
}
