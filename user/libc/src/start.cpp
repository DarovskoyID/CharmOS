// Старт процесса: стартовое сообщение "strt" (docs/abi/process.md §2) →
// argc/argv/environ, хендлы по ролям; затем main и exit.
#include <abi/process.hpp>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal.hpp"

// Во freestanding C++ main не особенная: программы объявляют её extern "C".
extern "C" int main(int argc, char** argv, char** envp);

// Точка входа (docs/abi/syscalls.md §7): RDI — стартовый хендл, RSP выровнен на 16.
asm(R"(
    .text
    .globl _start
    .type _start, @function
_start:
    xor     %ebp, %ebp
    call    __libc_start
    ud2
)");

char** environ = nullptr;

namespace libc {

namespace {

namespace ap = abi::process;

char g_msg[sizeof(ap::StartHeaderV2) + ap::kMaxArgsBytes + ap::kMaxEnvBytes];
char* g_argv[ap::kMaxArgs + 1];
StartInfo g_start = {};
ThreadBlock g_main_thread = {};

}  // namespace

StartInfo& start_info() { return g_start; }

}  // namespace libc

using namespace libc;

extern "C" [[noreturn]] void __libc_start(uint64_t boot) {
  g_main_thread.self = &g_main_thread;
  (void)charm::thread_set_fs(reinterpret_cast<uint64_t>(&g_main_thread));

  int argc = 0;
  uint32_t envc = 0;
  const char* env = nullptr;
  Handle hs[ap::kMaxStartHandlesV2] = {};
  auto r = boot != 0 ? charm::channel_read(static_cast<Handle>(boot), g_msg, sizeof g_msg, hs, ap::kMaxStartHandlesV2,
                                           charm::kForever)
                     : charm::SysResult{Status::BadHandle, 0};
  uint32_t bytes = static_cast<uint32_t>(r.value >> 32);
  uint32_t nh = static_cast<uint32_t>(r.value);
  ap::StartHeaderV2 h{}; uint32_t header_bytes = 0;
  if (r.ok() && ap::unpack_start_header(g_msg, bytes, &h, &header_bytes)) {
    if (h.handle_count == nh) {
      char* p = g_msg + header_bytes;
      for (uint32_t i = 0; i < h.argc && i < ap::kMaxArgs; ++i) {
        g_argv[argc++] = p;
        p += strlen(p) + 1;
      }
      env = g_msg + header_bytes + h.args_bytes;
      envc = h.envc;
      for (uint32_t i = 0; i < nh; ++i) {
        switch (h.roles[i]) {
          case ap::kRoleRoot: g_start.root = hs[i]; break;
          case ap::kRoleCwd: g_start.cwd = hs[i]; break;
          case ap::kRoleConsole: g_start.console = hs[i]; break;
          case ap::kRoleNet: g_start.net = hs[i]; break;
          case ap::kRoleLog: g_start.log = hs[i]; break;
          case ap::kRoleJob: g_start.job = hs[i]; break;
          case ap::kRoleSystem: g_start.system = hs[i]; break;
          case ap::kRolePower: g_start.power = hs[i]; break;
          case ap::kRoleDisplay: g_start.display = hs[i]; break;
          case ap::kRoleInput: g_start.input = hs[i]; break;
          case ap::kRoleWm: g_start.wm = hs[i]; break;
          case ap::kRoleAudio: g_start.audio = hs[i]; break;
          default: g_start.extra[g_start.extra_count++] = hs[i]; break;  // без роли: программа знает сама
        }
      }
    }
  }
  g_argv[argc] = nullptr;
  // Окружение — копия в куче (setenv меняет его).
  environ = static_cast<char**>(calloc(envc + 1, sizeof(char*)));
  for (uint32_t i = 0; environ != nullptr && i < envc; ++i) {
    environ[i] = strdup(env);
    env += strlen(env) + 1;
  }
  if (const char* pwd = getenv("PWD")) set_cwd(pwd);
  stdio_init();
  run_initializers();
  exit(main(argc, g_argv, environ));
}

extern "C" {

int atexit(void (*fn)(void)) {
  return register_exit(fn);
}

void exit(int code) {
  run_exit_handlers();
  run_finalizers();
  stdio_flush_all();
  charm::process_exit(code);
}

void _Exit(int code) { charm::process_exit(code); }
void _exit(int code) { charm::process_exit(code); }
void abort(void) { charm::process_exit(134); }

uint32_t charm_root(void) { return g_start.root; }
uint32_t charm_console(void) { return g_start.console; }
uint32_t charm_net(void) { return g_start.net; }
uint32_t charm_log(void) { return g_start.log; }
uint32_t charm_job(void) { return g_start.job; }
uint32_t charm_system(void) { return g_start.system; }
uint32_t charm_power(void) { return g_start.power; }
uint32_t charm_display(void) { return g_start.display; }
uint32_t charm_input(void) { return g_start.input; }
uint32_t charm_wm(void) { return g_start.wm; }
uint32_t charm_audio(void) { return g_start.audio; }
uint32_t charm_extra_handle(int i) {
  return i >= 0 && static_cast<uint32_t>(i) < g_start.extra_count ? g_start.extra[i] : 0;
}

}  // extern "C"
