#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" long pathconf(const char* path,int name) {
  if(name!=_PC_NAME_MAX) { errno=EINVAL; return -1; }
  struct stat information{};
  if(stat(path,&information)) return -1;
  return NAME_MAX;
}
extern "C" int symlink(const char* target,const char* path) {
  errno=target && path ? ENOTSUP : EFAULT;
  return -1;
}
extern "C" ssize_t readlink(const char* path,char* buffer,size_t capacity) {
  if(!buffer && capacity) { errno=EFAULT; return -1; }
  struct stat information{};
  if(lstat(path,&information)) return -1;
  // All native filesystem nodes are regular files/directories, never links.
  errno=EINVAL;
  return -1;
}
extern "C" int chmod(const char* path,mode_t) {
  struct stat information{};
  if(stat(path,&information)) return -1;
  // Authority belongs to capabilities, not fabricated Unix permission bits.
  errno=ENOTSUP;
  return -1;
}

extern "C" int fchmod(int fd,mode_t) {
  if(fd<0) { errno=EBADF; return -1; }
  // Authority belongs to capabilities, not fabricated Unix permission bits.
  errno=ENOTSUP;
  return -1;
}

extern "C" int fchown(int fd,uid_t,gid_t) {
  if(fd<0) { errno=EBADF; return -1; }
  // Ownership is capability-based; accept the call as a no-op.
  return 0;
}

extern "C" int chown(const char* path,uid_t,gid_t) {
  struct stat information{};
  if(!path) { errno=EFAULT; return -1; }
  if(stat(path,&information)) return -1;
  // Ownership is capability-based; accept the call as a no-op.
  return 0;
}

extern "C" int lchown(const char* path,uid_t owner,gid_t group) {
  return chown(path,owner,group);
}

extern "C" int utimes(const char* path,const struct timeval[2]) {
  struct stat information{};
  if(!path) { errno=EFAULT; return -1; }
  if(stat(path,&information)) return -1;
  // Timestamps are not persisted by the native filesystem yet.
  return 0;
}
