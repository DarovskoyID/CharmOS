// POSIX portability shims that CharmOS cannot back with a richer kernel
// contract yet. They are deliberately honest: locks and timestamps are
// process-local/no-persist, while capability rights remain the authority.
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/auxv.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>

extern "C" int flock(int fd, int) {
  if (fd < 0) {
    errno = EBADF;
    return -1;
  }
  // Advisory locks are not enforced across processes on CharmOS; every
  // capability holder already has whatever access the handle grants.
  return 0;
}

extern "C" unsigned long getauxval(unsigned long) { return 0; }

extern "C" int utime(const char* path, const struct utimbuf*) {
  struct stat information{};
  if (!path) {
    errno = EFAULT;
    return -1;
  }
  if (stat(path, &information)) return -1;
  // Timestamps are not persisted by the native filesystem yet.
  return 0;
}

extern "C" int kill(pid_t pid, int signo) {
  if (signo <= 0 || signo >= NSIG) {
    errno = EINVAL;
    return -1;
  }
  if (pid == 0 || pid == getpid()) {
    // CharmOS process authority is capability based: a process can always
    // terminate itself. Match the POSIX exit status convention.
    _exit(128 + signo);
  }
  errno = ESRCH;
  return -1;
}

extern "C" int raise(int signo) { return kill(getpid(), signo); }

// CharmOS authorises work through capabilities, not a local account database.
// Code that only inspects an owner (for diagnostics) gets "no record" rather
// than fabricated names.
extern "C" struct passwd* getpwuid(uid_t) {
  errno = ENOENT;
  return nullptr;
}

extern "C" struct passwd* getpwnam(const char*) {
  errno = ENOENT;
  return nullptr;
}

extern "C" struct group* getgrgid(gid_t) {
  errno = ENOENT;
  return nullptr;
}

extern "C" struct group* getgrnam(const char*) {
  errno = ENOENT;
  return nullptr;
}

extern "C" int prctl(int option, ...) {
  switch (option) {
    case PR_GET_DUMPABLE:
      // All capability holders may inspect their own address space.
      return 1;
    case PR_SET_DUMPABLE:
    case PR_SET_PDEATHSIG:
    case PR_SET_NAME:
    case PR_SET_NO_NEW_PRIVS:
      return 0;
    default:
      errno = ENOTSUP;
      return -1;
  }
}

extern "C" void sync(void) {
  // Native CharmOS filesystems complete writes before the syscall returns.
}

extern "C" int pause(void) {
  for (;;) {
    // Sleep until a signal interrupts us; CharmOS has no dedicated pause
    // syscall, and signal delivery interrupts nanosleep.
    struct timespec delay{3600, 0};
    if (nanosleep(&delay, nullptr) == 0) continue;
    if (errno == EINTR) return -1;
  }
}
