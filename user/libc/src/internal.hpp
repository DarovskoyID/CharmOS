#pragma once
// Внутреннее устройство libc CharmOS: таблица дескрипторов поверх хендлов
// ядра, ошибки, пути, блокировки на futex.

#include <charm/sys.hpp>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

namespace libc {

using charm::Handle;
using charm::Status;

// --- ошибки ---------------------------------------------------------------------------
int errno_from(Status s);
// Ставит errno по статусу и возвращает -1.
inline int fail(Status s) {
  errno = errno_from(s);
  return -1;
}
inline int fail_errno(int e) {
  errno = e;
  return -1;
}

// --- блокировка (мьютекс на futex: 0 свободен, 1 занят, 2 занят и ждут) ---------------
struct Lock {
  volatile uint32_t state = 0;
  void lock();
  void unlock();
};
struct Guard {
  explicit Guard(Lock& l) : l_(l) { l_.lock(); }
  ~Guard() { l_.unlock(); }
  Lock& l_;
};

// --- дескрипторы -------------------------------------------------------------------------
enum class Kind : uint8_t { Free, File, Dir, Socket, Console, Debug };
struct Fd {
  Kind kind;
  bool append;
  bool readable;
  bool writable;
  Handle handle;
  uint64_t offset;
  uint32_t refs;  // dup делит запись
};
constexpr int kMaxFds = 256;
Fd* fd_get(int fd);
int fd_alloc(Kind kind, Handle h, bool readable, bool writable);
Lock& fd_lock();

// --- пути ----------------------------------------------------------------------------------
// Абсолютный нормализованный путь без начального «/» (пустой — корень) в out.
bool resolve(const char* path, char* out, size_t cap);
const char* cwd();
void set_cwd(const char* abs_no_slash);

// --- запуск --------------------------------------------------------------------------------
struct StartInfo {
  Handle root, cwd, console, net, log, job;
  Handle extra[8];  // хендлы без роли, по порядку
  uint32_t extra_count;
};
StartInfo& start_info();

// Поток: блок TLS (по адресу %fs:0 — сам блок).
struct ThreadBlock {
  ThreadBlock* self;
  int err;
  void* (*fn)(void*);
  void* arg;
  void* ret;
  Handle handle;          // Thread
  volatile uint32_t done;  // 1 — завершён (для join)
  bool detached;
  uint64_t stack_top;
};
ThreadBlock* current_thread();

void stdio_init();
void stdio_flush_all();

}  // namespace libc
