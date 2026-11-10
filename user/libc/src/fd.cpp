// Дескрипторы, файлы и каталоги (docs/abi/fs.md), пути, время.
//
// Пути разрешаются в libc: текущий каталог — строка, все открытия идут от
// хендла корня (роль Root) по нормализованному пути. «..» выше корня — корень.
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "internal.hpp"

// v1.23 send deadline: kept out of charm/sys.hpp so the Chromium port does not
// invalidate every object that includes the native runtime header.
namespace charm {
inline SysResult socket_send_deadline(Handle s, const void* buf, uint64_t len,
                                      uint64_t deadline) {
  return syscall5(Number::SocketSendDeadline, s,
                  reinterpret_cast<uint64_t>(buf), len, 0, deadline);
}
}  // namespace charm

namespace libc {

namespace fsabi = abi::fs;

namespace {

struct FdEntry { Fd* description; bool cloexec; };
FdEntry g_fds[kMaxFds] = {};
Fd g_descriptions[kMaxFds] = {};
Lock g_fd_lock;
int g_fd_limit=kMaxFds;
int g_fd_hard_limit=kMaxFds;
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
  if (value->kind==Kind::Pipe) free(value->state);
  value->state=nullptr;
  value->kind = Kind::Free;
}
int duplicate_description(Fd* value, int minimum, bool cloexec) {
  if (minimum < 0 || minimum >= g_fd_limit) return fail_errno(EINVAL);
  for (int i = minimum; i < g_fd_limit; ++i) {
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

int fd_alloc(Kind kind, Handle h, bool readable, bool writable, int flags, void* state) {
  Guard g(g_fd_lock);
  for (int i = 0; i < g_fd_limit; ++i) {
    if (!g_fds[i].description) {
      for (auto& description : g_descriptions) {
        if (description.kind == Kind::Free) {
          description = {kind, (flags & O_APPEND) != 0, readable, writable,
                         (flags & O_NONBLOCK) != 0, h, 0, 1, {}, state};
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
  int descriptor;
  uint64_t cookie;
  uint8_t buf[2048];
  uint32_t pos, len;
  bool end;
  dirent ent;
};

namespace {
bool relative_name(const char* path,char* output,size_t capacity) {
  if(!path) { errno=EFAULT; return false; }
  size_t size=strnlen(path,capacity);
  if(size==capacity) { errno=ENAMETOOLONG; return false; }
  if(!size) { errno=ENOENT; return false; }
  size_t used=0;
  for(size_t at=0;at<size;) {
    while(at<size && path[at]=='/') ++at;
    size_t begin=at;
    while(at<size && path[at]!='/') ++at;
    size_t length=at-begin;
    if(!length || (length==1 && path[begin]=='.')) continue;
    if(length==2 && path[begin]=='.' && path[begin+1]=='.') { errno=ENOTSUP; return false; }
    if(used) output[used++]='/';
    memcpy(output+used,path+begin,length); used+=length;
  }
  output[used]=0;
  return true;
}

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

constexpr size_t kPipeAtomic=4096;
struct PipeState {
  unsigned char buffer[kPipeAtomic];
  size_t position, length;
  uint32_t buffered, pending, closed;
};
ssize_t pipe_read(Fd* description, void* buffer, size_t size) {
  Guard guard(description->position_lock);
  auto* state=static_cast<PipeState*>(description->state);
  if (state->position==state->length) {
    auto incoming=charm::channel_read(description->handle,state->buffer,kPipeAtomic,nullptr,0,read_deadline(description));
    if (!incoming.ok()) {
      if (incoming.status==Status::PeerClosed) {
        __atomic_store_n(&state->closed,1u,__ATOMIC_RELEASE);
        return 0;
      }
      return read_error(description,incoming.status);
    }
    state->position=0;
    state->length=static_cast<size_t>(incoming.value>>32);
    __atomic_store_n(&state->buffered,static_cast<uint32_t>(state->length),__ATOMIC_RELEASE);
    unsigned char ack=0;
    Status status=charm::channel_write(description->handle,&ack,1,nullptr,0);
    if (status!=Status::Ok && status!=Status::PeerClosed) return fail(status);
  }
  size_t available=state->length-state->position;
  size_t amount=size<available ? size : available;
  memcpy(buffer,state->buffer+state->position,amount);
  state->position+=amount;
  __atomic_store_n(&state->buffered,static_cast<uint32_t>(available-amount),__ATOMIC_RELEASE);
  return static_cast<ssize_t>(amount);
}
Status pipe_ack(Fd* description, uint64_t deadline) {
  auto* state=static_cast<PipeState*>(description->state);
  unsigned char ack;
  auto incoming=charm::channel_read(description->handle,&ack,1,nullptr,0,deadline);
  if (incoming.ok()) {
    if (incoming.value!=(uint64_t{1}<<32) || !__atomic_load_n(&state->pending,__ATOMIC_RELAXED))
      return Status::IoError;
    __atomic_sub_fetch(&state->pending,1u,__ATOMIC_RELEASE);
  } else if (incoming.status==Status::PeerClosed) {
    __atomic_store_n(&state->closed,1u,__ATOMIC_RELEASE);
  }
  return incoming.status;
}
ssize_t pipe_write(Fd* description, const void* buffer, size_t size) {
  Guard guard(description->position_lock);
  auto* state=static_cast<PipeState*>(description->state);
  if (size>INT64_MAX) return fail_errno(EINVAL);
  size_t done=0;
  while (done<size) {
    Status ack;
    while ((ack=pipe_ack(description,0))==Status::Ok) {}
    if (ack!=Status::WouldBlock && ack!=Status::TimedOut)
      return done ? static_cast<ssize_t>(done) : fail(ack);
    size_t amount=size-done<kPipeAtomic ? size-done : kPipeAtomic;
    Status status=charm::channel_write(description->handle,static_cast<const char*>(buffer)+done,amount,nullptr,0);
    if (status==Status::Ok) {
      __atomic_add_fetch(&state->pending,1u,__ATOMIC_RELEASE);
      done+=amount;
      continue;
    }
    if (status==Status::WouldBlock && !__atomic_load_n(&description->nonblock,__ATOMIC_RELAXED)) {
      Status waited=pipe_ack(description,charm::kForever);
      if (waited==Status::Ok) continue;
      return done ? static_cast<ssize_t>(done) : fail(waited);
    }
    if (status==Status::PeerClosed) __atomic_store_n(&state->closed,1u,__ATOMIC_RELEASE);
    return done ? static_cast<ssize_t>(done) : fail(status);
  }
  return static_cast<ssize_t>(done);
}
}

extern "C" {

int getrlimit(int resource, struct rlimit* limit) {
  if (!limit) return fail_errno(EFAULT);
  if (resource==RLIMIT_NOFILE) {
    Guard guard(fd_lock());
    *limit={static_cast<rlim_t>(g_fd_limit),static_cast<rlim_t>(g_fd_hard_limit)};
    return 0;
  }
  if (resource==RLIMIT_STACK) { *limit={65536,65536}; return 0; }
  if (resource==RLIMIT_CORE) { *limit={0,0}; return 0; }
  if (resource==RLIMIT_DATA) { *limit={RLIM_INFINITY,RLIM_INFINITY}; return 0; }
  return fail_errno(EINVAL);
}
int setrlimit(int resource,const struct rlimit* limit) {
  if (!limit) return fail_errno(EFAULT);
  if (limit->rlim_cur>limit->rlim_max) return fail_errno(EINVAL);
  if (resource==RLIMIT_NOFILE) {
    Guard guard(fd_lock());
    if (limit->rlim_max>static_cast<rlim_t>(g_fd_hard_limit)) return fail_errno(EPERM);
    g_fd_limit=static_cast<int>(limit->rlim_cur);
    g_fd_hard_limit=static_cast<int>(limit->rlim_max);
    return 0;
  }
  if (resource==RLIMIT_STACK)
    return limit->rlim_cur==65536 && limit->rlim_max==65536 ? 0 : fail_errno(ENOTSUP);
  if (resource==RLIMIT_CORE)
    return !limit->rlim_cur && !limit->rlim_max ? 0 : fail_errno(EPERM);
  if (resource==RLIMIT_DATA)
    return limit->rlim_cur==RLIM_INFINITY && limit->rlim_max==RLIM_INFINITY
               ? 0 : fail_errno(ENOTSUP);
  return fail_errno(EINVAL);
}
int getrusage(int who, struct rusage* usage) {
  if (!usage) return fail_errno(EFAULT);
  if (who!=RUSAGE_SELF && who!=RUSAGE_CHILDREN) return fail_errno(EINVAL);
  // No peak-RSS or split user/system accounting exists. V8 handles an
  // unavailable result; a successful all-zero snapshot would be fabricated.
  return fail_errno(ENOTSUP);
}

int open(const char* path, int flags, ...) {
  if(!path) return fail_errno(EFAULT);
  if(!*path) return fail_errno(ENOENT);
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

int openat(int directory,const char* path,int flags,...) {
  if(!path) return fail_errno(EFAULT);
  if(!*path) return fail_errno(ENOENT);
  constexpr int allowed=O_ACCMODE|O_CREAT|O_EXCL|O_TRUNC|O_APPEND|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_NOCTTY|O_NOFOLLOW;
  if(flags&~allowed) return fail_errno(EINVAL);
  if(directory==AT_FDCWD || path[0]=='/') return open(path,flags);
  FdLease parent(directory);
  if(!parent) return fail_errno(EBADF);
  if(parent->kind!=Kind::Dir) return fail_errno(ENOTDIR);
  char normalized[fsabi::kMaxPath];
  if(!relative_name(path,normalized,sizeof normalized)) return -1;
  int access_mode=flags&O_ACCMODE;
  if(access_mode==O_ACCMODE) return fail_errno(EINVAL);
  uint64_t native=access_mode!=O_RDONLY ? fsabi::kOpenWrite : 0;
  if(flags&O_CREAT) native|=fsabi::kOpenCreate;
  if(flags&O_EXCL) native|=fsabi::kOpenExclusive;
  if((flags&O_TRUNC) && access_mode!=O_RDONLY) native|=fsabi::kOpenTruncate;
  bool is_directory=(flags&O_DIRECTORY)!=0;
  if(is_directory) native=(native&~(fsabi::kOpenWrite|fsabi::kOpenTruncate))|fsabi::kOpenDirectory;
  auto object=charm::fs_open(parent->handle,normalized,native);
  if(object.status==Status::WrongType && !is_directory && access_mode==O_RDONLY && !(flags&(O_CREAT|O_TRUNC))) {
    object=charm::fs_open(parent->handle,normalized,native|fsabi::kOpenDirectory);
    is_directory=object.ok();
  }
  if(!object.ok()) return object.status==Status::WrongType ? fail_errno((flags&O_DIRECTORY) ? ENOTDIR : EISDIR) : fail(object.status);
  auto handle=static_cast<Handle>(object.value);
  int descriptor=fd_alloc(is_directory ? Kind::Dir : Kind::File,handle,access_mode!=O_WRONLY,access_mode!=O_RDONLY,flags);
  if(descriptor<0) (void)charm::handle_close(handle);
  return descriptor;
}

int fstatat(int directory,const char* path,struct stat* information,int flags) {
  if(flags&~AT_SYMLINK_NOFOLLOW) return fail_errno(EINVAL);
  if(!information || !path) return fail_errno(EFAULT);
  int descriptor=openat(directory,path,O_RDONLY|O_CLOEXEC);
  if(descriptor<0) return -1;
  int result=fstat(descriptor,information);
  int error=errno;
  (void)close(descriptor);
  errno=error;
  return result;
}

int unlinkat(int directory,const char* path,int flags) {
  if(flags&~AT_REMOVEDIR) return fail_errno(EINVAL);
  if(!path) return fail_errno(EFAULT);
  bool want_directory=(flags&AT_REMOVEDIR)!=0;
  char normalized[fsabi::kMaxPath];
  Handle parent_handle=0;
  FdLease parent(directory==AT_FDCWD || path[0]=='/' ? -1 : directory);
  if(directory==AT_FDCWD || path[0]=='/') {
    parent_handle=start_info().root;
    if(!parent_handle) return fail_errno(ENOENT);
    if(!path[0]) return fail_errno(ENOENT);
    if(!resolve(path,normalized,sizeof normalized)) return fail_errno(EINVAL);
  } else {
    if(!parent) return fail_errno(EBADF);
    if(parent->kind!=Kind::Dir) return fail_errno(ENOTDIR);
    parent_handle=parent->handle;
    if(!relative_name(path,normalized,sizeof normalized)) return -1;
  }
  if(!normalized[0]) return fail_errno(EINVAL);
  auto status=charm::fs_unlink_typed(parent_handle,normalized,want_directory);
  if(status==Status::WrongType) return fail_errno(want_directory ? ENOTDIR : EISDIR);
  return status==Status::Ok ? 0 : fail(status);
}

int renameat(int source_directory,const char* source,int target_directory,const char* target) {
  if(!source || !target) return fail_errno(EFAULT);
  if(!*source || !*target) return fail_errno(ENOENT);
  FdLease from(source_directory==AT_FDCWD || source[0]=='/' ? -1 : source_directory);
  FdLease to(target_directory==AT_FDCWD || target[0]=='/' ? -1 : target_directory);
  char from_name[fsabi::kMaxPath],to_name[fsabi::kMaxPath];
  Handle from_handle=0,to_handle=0;
  auto prepare=[&](int directory,const char* path,FdLease& lease,char* name,Handle* handle) {
    if(strnlen(path,fsabi::kMaxPath)==fsabi::kMaxPath) { errno=ENAMETOOLONG; return false; }
    if(directory==AT_FDCWD || path[0]=='/') {
      *handle=start_info().root;
      if(!*handle) { errno=ENOENT; return false; }
      if(!resolve(path,name,fsabi::kMaxPath)) { errno=ENAMETOOLONG; return false; }
    } else {
      if(!lease) { errno=EBADF; return false; }
      if(lease->kind!=Kind::Dir) { errno=ENOTDIR; return false; }
      *handle=lease->handle;
      if(!relative_name(path,name,fsabi::kMaxPath)) return false;
    }
    if(!*name) { errno=EINVAL; return false; }
    return true;
  };
  if(!prepare(source_directory,source,from,from_name,&from_handle) ||
     !prepare(target_directory,target,to,to_name,&to_handle)) return -1;
  auto status=charm::fs_rename(from_handle,from_name,to_handle,to_name);
  if(status==Status::WrongType) return fail_errno(EISDIR);
  if(status==Status::NotSupported) return fail_errno(ENOTSUP);
  return status==Status::Ok ? 0 : fail(status);
}
int rename(const char* source,const char* target) { return renameat(AT_FDCWD,source,AT_FDCWD,target); }

int pipe2(int descriptors[2], int flags) {
  if (!descriptors) return fail_errno(EFAULT);
  if (flags&~(O_CLOEXEC|O_NONBLOCK)) return fail_errno(EINVAL);
  auto* reader=static_cast<PipeState*>(calloc(1,sizeof(PipeState)));
  auto* writer=static_cast<PipeState*>(calloc(1,sizeof(PipeState)));
  if (!reader || !writer) { free(reader); free(writer); return fail_errno(ENOMEM); }
  Handle handles[2];
  Status status=charm::channel_create(handles);
  if (status!=Status::Ok) { free(reader); free(writer); return fail(status); }
  int rd=fd_alloc(Kind::Pipe,handles[0],true,false,flags,reader);
  if (rd<0) {
    int error=errno;
    (void)charm::handle_close(handles[0]); (void)charm::handle_close(handles[1]);
    free(reader); free(writer); return fail_errno(error);
  }
  int wr=fd_alloc(Kind::Pipe,handles[1],false,true,flags,writer);
  if (wr<0) {
    int error=errno;
    (void)close(rd); (void)charm::handle_close(handles[1]); free(writer);
    return fail_errno(error);
  }
  descriptors[0]=rd; descriptors[1]=wr;
  return 0;
}
int pipe(int descriptors[2]) { return pipe2(descriptors,0); }
int charm_open_fd_count(void) {
  Guard guard(fd_lock());
  int count=0;
  for(const auto& entry:g_fds) if(entry.description) ++count;
  return count;
}
int charm_channel_watch_fd(uint32_t endpoint,uint32_t interests) {
  if(!interests || (interests&~3u)) return fail_errno(EINVAL);
  abi::syscall::ChannelState information{};
  Status validation=charm::channel_query(endpoint,&information);
  // ChannelQuery checks object type before Read rights. A write-only channel
  // is valid for a writable watch even though its queue cannot be inspected.
  if(validation!=Status::Ok && validation!=Status::AccessDenied) return fail(validation);
  uint32_t requested=((interests&1u) ? charm::rights::kRead : 0u)|
                     ((interests&2u) ? charm::rights::kWrite : 0u);
  auto copy=charm::handle_duplicate(endpoint,requested);
  if(!copy.ok()) return fail(copy.status);
  Handle owned=static_cast<Handle>(copy.value);
  int descriptor=fd_alloc(Kind::ChannelWatch,owned,(interests&1u)!=0,(interests&2u)!=0,O_CLOEXEC|O_NONBLOCK);
  if(descriptor<0) { int error=errno; (void)charm::handle_close(owned); errno=error; }
  return descriptor;
}

int poll(struct pollfd* descriptors, nfds_t count, int timeout_ms) {
  static_assert(sizeof(pollfd)==8);
  if (count>64) return fail_errno(EINVAL);
  if (!descriptors && count) return fail_errno(EFAULT);
  struct Retained {
    Fd* values[64]{};
    ~Retained() { Guard guard(fd_lock()); for (auto* value:values) if (value) release_description(value); }
  } retained;
  {
    Guard guard(fd_lock());
    for (nfds_t i=0;i<count;++i) {
      descriptors[i].revents=0;
      if (descriptors[i].fd<0) continue;
      retained.values[i]=fd_get(descriptors[i].fd);
      if (retained.values[i]) ++retained.values[i]->refs;
      else descriptors[i].revents=POLLNVAL;
    }
  }
  for (nfds_t i=0;i<count;++i)
    if (retained.values[i] && (descriptors[i].events&~(POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL)))
      return fail_errno(ENOTSUP);
  auto ready_count=[&] {
    int ready=0;
    for (nfds_t i=0;i<count;++i) if (descriptors[i].revents) ++ready;
    return ready;
  };
  auto refresh=[&] {
    for (nfds_t i=0;i<count;++i) {
      Fd* value=retained.values[i];
      if (!value) continue;
      short wanted=descriptors[i].events;
      if (value->kind==Kind::Pipe) {
        auto* state=static_cast<PipeState*>(value->state);
        if (__atomic_load_n(&state->closed,__ATOMIC_ACQUIRE))
          descriptors[i].revents|=value->readable ? POLLHUP : POLLERR;
        if (value->readable && __atomic_load_n(&state->buffered,__ATOMIC_ACQUIRE))
          descriptors[i].revents|=wanted&POLLIN;
        if (value->writable && !__atomic_load_n(&state->closed,__ATOMIC_ACQUIRE) &&
            __atomic_load_n(&state->pending,__ATOMIC_ACQUIRE)<64)
          descriptors[i].revents|=wanted&POLLOUT;
      } else if (value->kind==Kind::File || value->kind==Kind::Dir || value->kind==Kind::Debug) {
        descriptors[i].revents|=wanted&(POLLIN|POLLOUT);
      } else if (value->kind==Kind::Console && value->writable) {
        descriptors[i].revents|=wanted&POLLOUT;
      }
    }
  };
  refresh();
  struct Watches {
    Handle port=0;
    Handle objects[64]{};
    uint32_t masks[64]{};
    size_t size=0;
    ~Watches() {
      if (!port) return;
      for (size_t i=0;i<size;++i) (void)charm::object_unbind(port,objects[i],i+1);
      (void)charm::handle_close(port);
    }
  } watches;
  int watch_index[64];
  for (nfds_t i=0;i<count;++i) {
    watch_index[i]=-1;
    Fd* value=retained.values[i];
    if (!value) continue;
    uint32_t mask=0;
    if (value->kind==Kind::Socket || value->kind==Kind::ChannelWatch) {
      if ((descriptors[i].events&POLLIN) && (value->kind!=Kind::ChannelWatch || value->readable)) mask|=1;
      if ((descriptors[i].events&POLLOUT) && (value->kind!=Kind::ChannelWatch || value->writable)) mask|=2;
    } else if (value->kind==Kind::Console) {
      if (value->readable && (descriptors[i].events&POLLIN)) mask=1;
    } else if (value->kind==Kind::Pipe) {
      if ((value->readable && (descriptors[i].events&POLLIN)) ||
          (value->writable && (descriptors[i].events&POLLOUT))) mask=1;
    }
    if (!mask) continue;
    size_t index=0;
    while (index<watches.size && watches.objects[index]!=value->handle) ++index;
    if (index==watches.size) { watches.objects[index]=value->handle; ++watches.size; }
    watches.masks[index]|=mask;
    watch_index[i]=static_cast<int>(index);
  }
  auto created=charm::port_create();
  if (!created.ok()) return fail(created.status);
  watches.port=static_cast<Handle>(created.value);
  for (size_t i=0;i<watches.size;++i) {
    Status status=charm::object_watch(watches.port,watches.objects[i],i+1,watches.masks[i]);
    if (status!=Status::Ok) return fail(status);
  }
  refresh(); // A concurrent reader may have buffered data while watches armed.
  uint64_t deadline=charm::kForever;
  if (timeout_ms>=0) deadline=charm::now_ns()+static_cast<uint64_t>(timeout_ms)*1000000;
  for (;;) {
    charm::PortPacket packet{};
    Status status=charm::port_wait(watches.port,&packet,ready_count() || timeout_ms==0 ? 0 : deadline);
    if (status==Status::TimedOut || status==Status::WouldBlock) return ready_count();
    if (status!=Status::Ok) return fail(status);
    if (!packet.key || packet.key>watches.size) return fail_errno(EIO);
    size_t index=static_cast<size_t>(packet.key-1);
    for (nfds_t i=0;i<count;++i) {
      if (watch_index[i]!=static_cast<int>(index)) continue;
      Fd* value=retained.values[i];
      short events=0;
      if (packet.type==abi::syscall::kPacketPeerClosed) {
        events=value->readable ? POLLHUP : POLLERR;
        if (value->readable && packet.data0) events|=POLLIN;
      }
      else if (value->kind==Kind::Socket) {
        if (packet.data0&1u) events|=POLLIN;
        if (packet.data0&2u) events|=POLLOUT;
      } else if(value->kind==Kind::ChannelWatch) {
        events=packet.type==abi::syscall::kPacketChannelWritable ? POLLOUT : POLLIN;
      } else if (value->kind==Kind::Pipe && value->writable) events=POLLOUT;
      else events=POLLIN;
      descriptors[i].revents|=events&(descriptors[i].events|POLLERR|POLLHUP);
    }
    refresh();
  }
}

int access(const char* path, int mode) {
  if (!path) return fail_errno(EFAULT);
  if ((mode & ~(R_OK | W_OK | X_OK)) != 0) return fail_errno(EINVAL);
  if ((mode & X_OK) != 0) return fail_errno(ENOTSUP);
  if (mode == F_OK) { struct stat info; return stat(path, &info); }
  int fd = open(path, (mode & W_OK) ? ((mode & R_OK) ? O_RDWR : O_WRONLY) : O_RDONLY);
  if (fd < 0) return -1;
  return close(fd);
}

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
    case Kind::Pipe: return pipe_read(f,buf,n);
    case Kind::ChannelWatch: return fail_errno(ENOTSUP);
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
  if (n==0) return 0;
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
    case Kind::Pipe: return pipe_write(f,buf,n);
    case Kind::ChannelWatch: return fail_errno(ENOTSUP);
    case Kind::Socket: {
      const bool nonblock = __atomic_load_n(&f->nonblock, __ATOMIC_RELAXED);
      const uint64_t deadline = nonblock ? 0 : charm::kForever;
      size_t done = 0;
      for (;;) {
        auto r = charm::socket_send_deadline(
            f->handle, static_cast<const char*>(buf) + done, n - done, deadline);
        if (!r.ok()) {
          if (nonblock &&
              (r.status == Status::WouldBlock || r.status == Status::TimedOut)) {
            return done != 0 ? static_cast<ssize_t>(done) : fail_errno(EAGAIN);
          }
          return done != 0 ? static_cast<ssize_t>(done) : fail(r.status);
        }
        if (r.value == 0) return done != 0 ? static_cast<ssize_t>(done) : fail_errno(EIO);
        done += r.value;
        if (done == n || nonblock) return static_cast<ssize_t>(done);
      }
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
  if (f == nullptr || to < 0 || to >= g_fd_limit) return fail_errno(EBADF);
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
  static_assert(sizeof(struct stat)==96 && __builtin_offsetof(struct stat,st_ino)==8);
  if (!st) return fail_errno(EFAULT);
  FdLease lease(fd);
  Fd* f = lease.get();
  if (f == nullptr) return fail_errno(EBADF);
  if(f->kind==Kind::ChannelWatch) return fail_errno(ENOTSUP);
  if (f->kind != Kind::File && f->kind != Kind::Dir) {
    *st = {};
    st->st_mode=f->kind==Kind::Pipe ? S_IFIFO|0600u : 0u;
    return 0;
  }
  abi::fs::Stat s;
  Status r = charm::fs_stat(f->handle, &s);
  if (r != Status::Ok) return fail(r);
  if (s.size > INT64_MAX) return fail_errno(EOVERFLOW);
  auto identity=charm::fs_identity(f->handle);
  if (!identity.ok()) return fail(identity.status);
  *st={};
  st->st_dev=(identity.value>>56)+1;
  st->st_ino=identity.value&((uint64_t{1}<<56)-1);
  st->st_size = static_cast<off_t>(s.size);
  st->st_mode = s.type == fsabi::kTypeDirectory ? S_IFDIR | 0755 : S_IFREG | 0644;
  st->st_nlink = 1;
  st->st_uid = 0;
  st->st_gid = 0;
  st->st_blksize = 4096;
  st->st_blocks = static_cast<blkcnt_t>((s.size + 511) / 512);
  st->st_mtime = static_cast<time_t>(s.mtime_ns / 1'000'000'000);
  return 0;
}

int stat(const char* path, struct stat* st) {
  if (!path || !st) return fail_errno(EFAULT);
  Handle h;
  bool dir;
  Status s = open_node(path, 0, &h, &dir);
  if (s != Status::Ok) return fail(s == Status::WrongType ? Status::NotFound : s);
  abi::fs::Stat t;
  s = charm::fs_stat(h, &t);
  auto identity=charm::fs_identity(h);
  (void)charm::handle_close(h);
  if (s != Status::Ok) return fail(s);
  if (!identity.ok()) return fail(identity.status);
  if (t.size > INT64_MAX) return fail_errno(EOVERFLOW);
  *st={};
  st->st_dev=(identity.value>>56)+1;
  st->st_ino=identity.value&((uint64_t{1}<<56)-1);
  st->st_size = static_cast<off_t>(t.size);
  st->st_mode = dir ? S_IFDIR | 0755 : S_IFREG | 0644;
  st->st_nlink = 1;
  st->st_uid = 0;
  st->st_gid = 0;
  st->st_blksize = 4096;
  st->st_blocks = static_cast<blkcnt_t>((t.size + 511) / 512);
  st->st_mtime = static_cast<time_t>(t.mtime_ns / 1'000'000'000);
  return 0;
}

int lstat(const char* path, struct stat* st) { return stat(path,st); }
int futimes(int fd, const struct timeval[2]) {
  FdLease lease(fd);
  return lease ? fail_errno(ENOTSUP) : fail_errno(EBADF);
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
  return unlinkat(AT_FDCWD,path,0);
}

int rmdir(const char* path) { return unlinkat(AT_FDCWD,path,AT_REMOVEDIR); }
int remove(const char* path) {
  int result=unlink(path);
  return result<0 && errno==EISDIR ? rmdir(path) : result;
}

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
int ftruncate64(int fd, off_t size) { return ftruncate(fd,size); }

DIR* opendir(const char* path) {
  int fd = open(path, O_RDONLY | O_DIRECTORY|O_CLOEXEC);
  if (fd < 0) return nullptr;
  DIR* directory=fdopendir(fd);
  if(!directory) { int error=errno; (void)close(fd); errno=error; }
  return directory;
}

DIR* fdopendir(int fd) {
  FdLease file(fd);
  if(!file || !file->readable) { errno=EBADF; return nullptr; }
  if(file->kind!=Kind::Dir) { errno=ENOTDIR; return nullptr; }
  auto* d = static_cast<DIR*>(calloc(1, sizeof(DIR)));
  if (d == nullptr) { errno=ENOMEM; return nullptr; }
  d->handle=file->handle;
  d->descriptor=fd;
  return d;
}

int dirfd(DIR* directory) { return directory ? directory->descriptor : fail_errno(EINVAL); }

struct dirent* readdir(DIR* d) {
  if(!d) { errno=EINVAL; return nullptr; }
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
  if(!d) return fail_errno(EINVAL);
  int result=close(d->descriptor);
  free(d);
  return result;
}

uint32_t charm_fd_handle(int fd) {
  FdLease lease(fd);
  Fd* f = lease.get();
  return f != nullptr ? f->handle : 0;
}

int charm_fd_from_handle(uint32_t h, int kind) {
  if(kind<1 || kind>3) return fail_errno(EINVAL);
  auto permissions=charm::handle_rights(h);
  if(!permissions.ok()) return fail(permissions.status);
  if(kind!=3) {
    abi::fs::Stat information{};
    auto status=charm::fs_stat(h,&information);
    if(status!=Status::Ok) return fail(status);
    if((information.type==abi::fs::kTypeDirectory)!=(kind==2)) return fail_errno(kind==2 ? ENOTDIR : EISDIR);
  }
  return fd_alloc(kind==3 ? Kind::Socket : (kind==2 ? Kind::Dir : Kind::File),h,
                  (permissions.value&charm::rights::kRead)!=0,(permissions.value&charm::rights::kWrite)!=0);
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
uid_t geteuid(void) { return 0; }
uid_t getuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }  // Суперпользователя нет: каждый — сам себе владелец.
int getpeereid(int, uid_t*, gid_t*) {
  // SO_PEERCRED/идентичность пира через сокет не поддержана (см. socket.md).
  return fail_errno(ENOTSUP);
}
int gethostname(char* name, size_t size) {
  if (name == nullptr) return fail_errno(EFAULT);
  utsname identity{};
  if (uname(&identity) < 0) return -1;
  size_t n = strlen(identity.nodename) + 1;
  if (size < n) return fail_errno(ENAMETOOLONG);
  memcpy(name, identity.nodename, n);
  return 0;
}

}  // extern "C"
