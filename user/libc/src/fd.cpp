// Дескрипторы, файлы и каталоги (docs/abi/fs.md), пути, время.
//
// Пути разрешаются в libc: текущий каталог — строка, все открытия идут от
// хендла корня (роль Root) по нормализованному пути. «..» выше корня — корень.
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "internal.hpp"

namespace libc {

namespace fsabi = abi::fs;

namespace {

struct FdEntry { Fd* description; bool cloexec; };
FdEntry g_fds[kMaxFds] = {};
Fd g_descriptions[kMaxFds] = {};
Lock g_fd_lock;
char g_cwd[fsabi::kMaxPath] = "";  // без начального «/»

}  // namespace

Lock& fd_lock() { return g_fd_lock; }

Fd* fd_get(int fd) {
  if (fd < 0 || fd >= kMaxFds) return nullptr;
  return g_fds[fd].description;
}

namespace {
// Caller holds the descriptor table lock, including for I/O lease releases.
void release_description(Fd* value) {
  if (--value->refs != 0) return;
  bool shared = false;
  for (auto& other : g_descriptions)
    if (&other != value && other.kind != Kind::Free && other.handle == value->handle)
      shared = true;
  if (!shared && value->kind != Kind::Debug && value->handle)
    (void)charm::handle_close(value->handle);
  value->kind = Kind::Free;
}
int duplicate_description(Fd* value, int minimum, bool cloexec) {
  if (minimum < 0 || minimum >= kMaxFds) return fail_errno(EINVAL);
  for (int i = minimum; i < kMaxFds; ++i) {
    if (!g_fds[i].description) {
      ++value->refs;
      g_fds[i] = {value, cloexec};
      return i;
    }
  }
  return fail_errno(EMFILE);
}
}
FdLease::FdLease(int fd) {
  Guard guard(fd_lock());
  value_ = fd_get(fd);
  if (value_) ++value_->refs;
}
FdLease::~FdLease() {
  if (value_) { Guard guard(fd_lock()); release_description(value_); }
}

int fd_alloc(Kind kind, Handle h, bool readable, bool writable, int flags) {
  Guard g(g_fd_lock);
  for (int i = 0; i < kMaxFds; ++i) {
    if (!g_fds[i].description) {
      for (auto& description : g_descriptions) {
        if (description.kind == Kind::Free) {
          description = {kind, (flags & O_APPEND) != 0, readable, writable,
                         (flags & O_NONBLOCK) != 0, h, 0, 1, {}};
          g_fds[i] = {&description, (flags & O_CLOEXEC) != 0};
          return i;
        }
      }
      return fail_errno(ENFILE);  // descriptions kept alive by in-progress I/O
    }
  }
  return fail_errno(EMFILE);
}

const char* cwd() { return g_cwd; }

void set_cwd(const char* p) {
  char tmp[fsabi::kMaxPath];
  if (resolve(p, tmp, sizeof tmp)) strcpy(g_cwd, tmp);
}

bool resolve(const char* path, char* out, size_t cap) {
  // Компоненты: из cwd (для относительного пути), затем из path.
  size_t len = 0;
  out[0] = '\0';
  auto push = [&](const char* c, size_t n) {
    if (n == 0 || (n == 1 && c[0] == '.')) return true;
    if (n == 2 && c[0] == '.' && c[1] == '.') {
      while (len > 0 && out[len - 1] != '/') --len;
      if (len > 0) --len;  // «/» перед компонентой
      out[len] = '\0';
      return true;
    }
    if (len + (len != 0 ? 1 : 0) + n + 1 > cap) return false;
    if (len != 0) out[len++] = '/';
    memcpy(out + len, c, n);
    len += n;
    out[len] = '\0';
    return true;
  };
  auto walk = [&](const char* p) {
    while (*p != '\0') {
      while (*p == '/') ++p;
      const char* s = p;
      while (*p != '\0' && *p != '/') ++p;
      if (!push(s, static_cast<size_t>(p - s))) return false;
    }
    return true;
  };
  if (path[0] != '/' && !walk(g_cwd)) return false;
  return walk(path);
}

namespace {

// Открыть узел по пути: is_dir — что получилось.
Status open_node(const char* path, uint64_t flags, Handle* out, bool* is_dir) {
  Handle root = start_info().root;
  if (root == 0) return Status::NotFound;
  char abs[fsabi::kMaxPath];
  if (!resolve(path, abs, sizeof abs)) return Status::InvalidArgs;
  size_t n = strlen(abs);
  if ((flags & fsabi::kOpenDirectory) == 0 && n != 0) {
    auto r = charm::fs_open(root, abs, n, flags);
    if (r.ok()) {
      *out = static_cast<Handle>(r.value);
      *is_dir = false;
      return Status::Ok;
    }
    if (r.status != Status::WrongType || (flags & fsabi::kOpenWrite) != 0) return r.status;
  }
  auto r = charm::fs_open(root, abs, n, (flags & ~(fsabi::kOpenTruncate | fsabi::kOpenWrite)) | fsabi::kOpenDirectory);
  if (!r.ok()) return r.status;
  *out = static_cast<Handle>(r.value);
  *is_dir = true;
  return Status::Ok;
}

}  // namespace

}  // namespace libc

using namespace libc;

struct __dir {
  Handle handle;
  uint64_t cookie;
  uint8_t buf[2048];
  uint32_t pos, len;
  bool end;
  dirent ent;
};

namespace {
ssize_t read_description(Fd* f, void* buf, size_t n, off_t off) {
  if (off < 0) return fail_errno(EINVAL);
  if (n > fsabi::kMaxTransfer) n = fsabi::kMaxTransfer;
  auto result = charm::file_read(f->handle, buf, n, static_cast<uint64_t>(off));
  return result.ok() ? static_cast<ssize_t>(result.value) : fail(result.status);
}
ssize_t write_description(Fd* f, const void* buf, size_t n, off_t off) {
  if (off < 0) return fail_errno(EINVAL);
  if (n > fsabi::kMaxTransfer) n = fsabi::kMaxTransfer;
  if (n > static_cast<uint64_t>(INT64_MAX - off)) return fail_errno(EOVERFLOW);
  auto result = charm::file_write(f->handle, buf, n, static_cast<uint64_t>(off));
  return result.ok() ? static_cast<ssize_t>(result.value) : fail(result.status);
}
uint64_t read_deadline(Fd* value) {
  return __atomic_load_n(&value->nonblock, __ATOMIC_RELAXED) ? 0 : charm::kForever;
}
int read_error(Fd* value, Status status) {
  return status == Status::TimedOut && __atomic_load_n(&value->nonblock, __ATOMIC_RELAXED)
      ? fail_errno(EAGAIN) : fail(status);
}
}

extern "C" {

int open(const char* path, int flags, ...) {
  uint64_t f = 0;
  int acc = flags & O_ACCMODE;
  if (acc == O_ACCMODE) return fail_errno(EINVAL);
  if (acc != O_RDONLY) f |= fsabi::kOpenWrite;
  if ((flags & O_CREAT) != 0) f |= fsabi::kOpenCreate;
  if ((flags & O_EXCL) != 0) f |= fsabi::kOpenExclusive;
  if ((flags & O_TRUNC) != 0 && acc != O_RDONLY) f |= fsabi::kOpenTruncate;
  if ((flags & O_DIRECTORY) != 0) f |= fsabi::kOpenDirectory;
  Handle h;
  bool dir;
  Status s = open_node(path, f, &h, &dir);
  if (s == Status::WrongType) return fail_errno((flags & O_DIRECTORY) != 0 ? ENOTDIR : EISDIR);
  if (s != Status::Ok) return fail(s);
  int fd = fd_alloc(dir ? Kind::Dir : Kind::File, h, acc != O_WRONLY, acc != O_RDONLY, flags);
  if (fd < 0) {
    (void)charm::handle_close(h);
    return -1;
  }
  return fd;
}

int creat(const char* path, mode_t) { return open(path, O_WRONLY | O_CREAT | O_TRUNC); }

ssize_t pread(int fd, void* buf, size_t n, off_t off) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr || !f->readable) return fail_errno(EBADF);
  if (f->kind == Kind::Dir) return fail_errno(EISDIR);
  if (f->kind != Kind::File) return fail_errno(ESPIPE);
  return read_description(f, buf, n, off);
}

ssize_t pwrite(int fd, const void* buf, size_t n, off_t off) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr || !f->writable) return fail_errno(EBADF);
  if (f->kind != Kind::File) return fail_errno(ESPIPE);
  return write_description(f, buf, n, off);
}

ssize_t read(int fd, void* buf, size_t n) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr || !f->readable) return fail_errno(EBADF);
  if (n == 0) return 0;
  switch (f->kind) {
    case Kind::File: {
      Guard guard(f->position_lock);
      if (f->offset > INT64_MAX) return fail_errno(EOVERFLOW);
      ssize_t got = read_description(f, buf, n, static_cast<off_t>(f->offset));
      if (got > 0) f->offset += static_cast<uint64_t>(got);
      return got;
    }
    case Kind::Console: {
      auto r = charm::console_read(f->handle, buf, n, read_deadline(f));
      if (!r.ok()) return read_error(f, r.status);
      return static_cast<ssize_t>(r.value);
    }
    case Kind::Socket: {
      auto r = charm::socket_recv(f->handle, buf, n, read_deadline(f));
      if (!r.ok()) return r.status == Status::PeerClosed ? 0 : read_error(f, r.status);
      return static_cast<ssize_t>(r.value);
    }
    case Kind::Debug:
      return 0;  // консоли нет: ввод пуст
    case Kind::Dir:
      return fail_errno(EISDIR);
    case Kind::Free:
      break;
  }
  return fail_errno(EBADF);
}

ssize_t write(int fd, const void* buf, size_t n) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr || !f->writable) return fail_errno(EBADF);
  switch (f->kind) {
    case Kind::File: {
      Guard guard(f->position_lock);
      if (__atomic_load_n(&f->append, __ATOMIC_RELAXED)) {
        abi::fs::Stat st;
        Status status = charm::fs_stat(f->handle, &st);
        if (status != Status::Ok) return fail(status);
        f->offset = st.size;
      }
      if (f->offset > INT64_MAX) return fail_errno(EOVERFLOW);
      ssize_t done = write_description(f, buf, n, static_cast<off_t>(f->offset));
      if (done > 0) f->offset += static_cast<uint64_t>(done);
      return done;
    }
    case Kind::Console: {
      auto r = charm::console_write(f->handle, buf, n);
      if (!r.ok()) return fail(r.status);
      return static_cast<ssize_t>(r.value);
    }
    case Kind::Socket: {
      size_t done = 0;
      while (done < n) {
        auto r = charm::socket_send(f->handle, static_cast<const char*>(buf) + done, n - done);
        if (!r.ok()) return done != 0 ? static_cast<ssize_t>(done) : fail(r.status);
        done += r.value;
      }
      return static_cast<ssize_t>(done);
    }
    case Kind::Debug: {
      for (size_t done = 0; done < n;) {
        size_t chunk = n - done < abi::syscall::kDebugWriteMax ? n - done : abi::syscall::kDebugWriteMax;
        (void)charm::debug_write(static_cast<const char*>(buf) + done, chunk);
        done += chunk;
      }
      return static_cast<ssize_t>(n);
    }
    case Kind::Dir:
      return fail_errno(EISDIR);
    case Kind::Free:
      break;
  }
  return fail_errno(EBADF);
}

int close(int fd) {
  Guard g(fd_lock());
  Fd* f = fd_get(fd);
  if (f == nullptr) return fail_errno(EBADF);
  g_fds[fd] = {};
  release_description(f);
  return 0;
}

off_t lseek(int fd, off_t off, int whence) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr) return fail_errno(EBADF);
  if (f->kind != Kind::File) return fail_errno(ESPIPE);
  if (whence < 0 || whence > 2) return fail_errno(EINVAL);
  Guard position_guard(f->position_lock);
  int64_t base = 0;
  if (whence == 1) {
    if (f->offset > INT64_MAX) return fail_errno(EOVERFLOW);
    base = static_cast<int64_t>(f->offset);
  }
  if (whence == 2) {
    abi::fs::Stat st;
    Status s = charm::fs_stat(f->handle, &st);
    if (s != Status::Ok) return fail(s);
    if (st.size > INT64_MAX) return fail_errno(EOVERFLOW);
    base = static_cast<int64_t>(st.size);
  }
  if (off < -base) return fail_errno(EINVAL);
  if (off > 0 && base > INT64_MAX - off) return fail_errno(EOVERFLOW);
  f->offset = static_cast<uint64_t>(base + off);
  return static_cast<off_t>(f->offset);
}

int dup2(int fd, int to) {
  Guard g(fd_lock());
  Fd* f = fd_get(fd);
  if (f == nullptr || to < 0 || to >= kMaxFds) return fail_errno(EBADF);
  if (fd == to) return to;
  if (g_fds[to].description) release_description(g_fds[to].description);
  g_fds[to] = {f, false};
  ++f->refs;
  return to;
}

int dup(int fd) {
  Guard guard(fd_lock());
  Fd* value = fd_get(fd);
  return value ? duplicate_description(value, 0, false) : fail_errno(EBADF);
}

int fcntl(int fd, int command, ...) {
  Guard guard(fd_lock());
  Fd* value = fd_get(fd);
  if (!value) return fail_errno(EBADF);
  if (command == F_GETFD) return g_fds[fd].cloexec ? FD_CLOEXEC : 0;
  if (command == F_GETFL) {
    int mode = value->readable ? (value->writable ? O_RDWR : O_RDONLY) : O_WRONLY;
    if (__atomic_load_n(&value->append, __ATOMIC_RELAXED)) mode |= O_APPEND;
    if (__atomic_load_n(&value->nonblock, __ATOMIC_RELAXED)) mode |= O_NONBLOCK;
    if (value->kind == Kind::Dir) mode |= O_DIRECTORY;
    return mode;
  }
  if (command != F_SETFD && command != F_SETFL && command != F_DUPFD && command != F_DUPFD_CLOEXEC)
    return fail_errno(ENOTSUP);
  va_list args;
  va_start(args, command);
  int argument = va_arg(args, int);
  va_end(args);
  if (command == F_SETFD) {
    if (argument & ~FD_CLOEXEC) return fail_errno(EINVAL);
    g_fds[fd].cloexec = (argument & FD_CLOEXEC) != 0;
    return 0;
  }
  if (command == F_SETFL) {
    if (argument & ~(O_ACCMODE | O_APPEND | O_NONBLOCK)) return fail_errno(EINVAL);
    __atomic_store_n(&value->append, (argument & O_APPEND) != 0, __ATOMIC_RELAXED);
    __atomic_store_n(&value->nonblock, (argument & O_NONBLOCK) != 0, __ATOMIC_RELAXED);
    return 0;
  }
  return duplicate_description(value, argument, command == F_DUPFD_CLOEXEC);
}

int fstat(int fd, struct stat* st) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr) return fail_errno(EBADF);
  if (f->kind != Kind::File && f->kind != Kind::Dir) {
    *st = {0, 0, 0};
    return 0;
  }
  abi::fs::Stat s;
  Status r = charm::fs_stat(f->handle, &s);
  if (r != Status::Ok) return fail(r);
  if (s.size > INT64_MAX) return fail_errno(EOVERFLOW);
  st->st_size = static_cast<off_t>(s.size);
  st->st_mode = s.type == fsabi::kTypeDirectory ? S_IFDIR | 0755 : S_IFREG | 0644;
  st->st_mtime = static_cast<time_t>(s.mtime_ns / 1'000'000'000);
  return 0;
}

int stat(const char* path, struct stat* st) {
  Handle h;
  bool dir;
  Status s = open_node(path, 0, &h, &dir);
  if (s != Status::Ok) return fail(s == Status::WrongType ? Status::NotFound : s);
  abi::fs::Stat t;
  s = charm::fs_stat(h, &t);
  (void)charm::handle_close(h);
  if (s != Status::Ok) return fail(s);
  if (t.size > INT64_MAX) return fail_errno(EOVERFLOW);
  st->st_size = static_cast<off_t>(t.size);
  st->st_mode = dir ? S_IFDIR | 0755 : S_IFREG | 0644;
  st->st_mtime = static_cast<time_t>(t.mtime_ns / 1'000'000'000);
  return 0;
}

int mkdir(const char* path, mode_t) {
  Handle h;
  bool dir;
  Status s = open_node(path, fsabi::kOpenDirectory | fsabi::kOpenCreate | fsabi::kOpenExclusive, &h, &dir);
  if (s != Status::Ok) return fail(s);
  (void)charm::handle_close(h);
  return 0;
}

int unlink(const char* path) {
  Handle root = start_info().root;
  char abs[fsabi::kMaxPath];
  if (root == 0 || !resolve(path, abs, sizeof abs) || abs[0] == '\0') return fail_errno(ENOENT);
  Status s = charm::fs_unlink(root, abs);
  return s == Status::Ok ? 0 : fail(s);
}

int rmdir(const char* path) { return unlink(path); }
int remove(const char* path) { return unlink(path); }

int chdir(const char* path) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  if (!S_ISDIR(st.st_mode)) return fail_errno(ENOTDIR);
  char abs[fsabi::kMaxPath];
  if (!resolve(path, abs, sizeof abs)) return fail_errno(EINVAL);
  strcpy(const_cast<char*>(libc::cwd()), abs);
  return 0;
}

char* getcwd(char* buf, size_t size) {
  size_t n = strlen(libc::cwd());
  if (n + 2 > size) {
    errno = ERANGE;
    return nullptr;
  }
  buf[0] = '/';
  memcpy(buf + 1, libc::cwd(), n + 1);
  return buf;
}

int isatty(int fd) {
  FdLease lease(fd);
  Fd* f = lease.get();
  return f != nullptr && f->kind == Kind::Console ? 1 : 0;
}

int fsync(int fd) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr || f->kind != Kind::File) return fail_errno(EBADF);
  Status s = charm::file_sync(f->handle);
  return s == Status::Ok ? 0 : fail(s);
}

int ftruncate(int fd, off_t size) {
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr || f->kind != Kind::File) return fail_errno(EBADF);
  if (size < 0) return fail_errno(EINVAL);
  Status s = charm::file_resize(f->handle, static_cast<uint64_t>(size));
  return s == Status::Ok ? 0 : fail(s);
}

DIR* opendir(const char* path) {
  int fd = open(path, O_RDONLY | O_DIRECTORY);
  if (fd < 0) return nullptr;
  auto* d = static_cast<DIR*>(calloc(1, sizeof(DIR)));
  if (d == nullptr) {
    close(fd);
    return nullptr;
  }
  // Хендл переходит DIR; дескриптор освобождаем без закрытия хендла.
  {
    Guard guard(fd_lock());
    Fd* value = fd_get(fd);
    d->handle = value->handle;
    value->handle = 0;
    g_fds[fd] = {};
    release_description(value);
  }
  return d;
}

struct dirent* readdir(DIR* d) {
  for (;;) {
    if (d->pos + sizeof(fsabi::DirEntryHeader) <= d->len) {
      auto* h = reinterpret_cast<fsabi::DirEntryHeader*>(d->buf + d->pos);
      if (h->record_len != 0) {
        d->ent.d_type = h->type == fsabi::kTypeDirectory ? DT_DIR : DT_REG;
        memcpy(d->ent.d_name, d->buf + d->pos + sizeof *h, h->name_len);
        d->ent.d_name[h->name_len] = '\0';
        d->pos += h->record_len;
        return &d->ent;
      }
    }
    if (d->end) return nullptr;
    auto r = charm::dir_read(d->handle, d->buf, sizeof d->buf, d->cookie);
    if (!r.ok()) {
      errno = errno_from(r.status);
      return nullptr;
    }
    d->pos = 0;
    d->len = sizeof d->buf;
    if (d->buf[0] == 0 && d->buf[1] == 0) d->end = true;  // пустой ответ — конец
    d->cookie = r.value;
  }
}

int closedir(DIR* d) {
  (void)charm::handle_close(d->handle);
  free(d);
  return 0;
}

uint32_t charm_fd_handle(int fd) {
  FdLease lease(fd);
  Fd* f = lease.get();
  return f != nullptr ? f->handle : 0;
}

int charm_fd_from_handle(uint32_t h, int kind) {
  return fd_alloc(kind == 3 ? Kind::Socket : Kind::File, h, true, true);
}

// --- время -----------------------------------------------------------------------------------

int clock_gettime(clockid_t id, struct timespec* ts) {
  if (!ts) return fail_errno(EFAULT);
  if (id != CLOCK_REALTIME && id != CLOCK_MONOTONIC && id != CLOCK_BOOTTIME &&
      id != CLOCK_PROCESS_CPUTIME_ID && id != CLOCK_THREAD_CPUTIME_ID) return fail_errno(EINVAL);
  uint64_t ns = 0;
  if (id == CLOCK_REALTIME) {
    auto result = charm::realtime();
    if (!result.ok()) return fail(result.status);
    ns = result.value;
  } else if (id == CLOCK_PROCESS_CPUTIME_ID || id == CLOCK_THREAD_CPUTIME_ID) {
    auto result = charm::runtime_query(static_cast<uint64_t>(id));
    if (!result.ok()) return fail(result.status);
    ns = result.value;
  } else ns = charm::now_ns();
  ts->tv_sec = static_cast<time_t>(ns / 1'000'000'000);
  ts->tv_nsec = static_cast<long>(ns % 1'000'000'000);
  return 0;
}

time_t time(time_t* t) {
  auto result = charm::realtime();
  if (!result.ok()) return static_cast<time_t>(fail(result.status));
  auto v = static_cast<time_t>(result.value / 1'000'000'000);
  if (t != nullptr) *t = v;
  return v;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
  uint64_t ns = static_cast<uint64_t>(req->tv_sec) * 1'000'000'000 + static_cast<uint64_t>(req->tv_nsec);
  uint64_t until = charm::now_ns() + ns;
  auto r = charm::syscall1(abi::syscall::Number::Nanosleep, until);
  if (r.status == Status::Interrupted) {
    uint64_t now = charm::now_ns();
    if (rem != nullptr) {
      uint64_t left = until > now ? until - now : 0;
      rem->tv_sec = static_cast<time_t>(left / 1'000'000'000);
      rem->tv_nsec = static_cast<long>(left % 1'000'000'000);
    }
    return fail_errno(EINTR);
  }
  return 0;
}

unsigned sleep(unsigned seconds) {
  struct timespec ts = {static_cast<time_t>(seconds), 0}, rem = {0, 0};
  if (nanosleep(&ts, &rem) != 0) return static_cast<unsigned>(rem.tv_sec);
  return 0;
}

int usleep(unsigned usec) {
  struct timespec ts = {static_cast<time_t>(usec / 1'000'000), static_cast<long>(usec % 1'000'000) * 1000};
  return nanosleep(&ts, nullptr);
}

static pid_t runtime_id(uint64_t selector) {
  auto result = charm::runtime_query(selector);
  if (!result.ok()) return static_cast<pid_t>(fail(result.status));
  if (result.value > INT32_MAX) return static_cast<pid_t>(fail_errno(EOVERFLOW));
  return static_cast<pid_t>(result.value);
}
pid_t getpid(void) { return runtime_id(0); }
pid_t gettid(void) { return runtime_id(1); }

}  // extern "C"
