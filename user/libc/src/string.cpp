// string.h, ctype.h, strerror.
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
int bcmp(const void* a,const void* b,size_t n) { return memcmp(a,b,n); }

void* memcpy(void* dst, const void* src, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  const auto* s = static_cast<const unsigned char*>(src);
  asm volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  const auto* s = static_cast<const unsigned char*>(src);
  if (d == s || n == 0) return dst;
  if (d < s || d >= s + n) return memcpy(dst, src, n);
  while (n-- != 0) d[n] = s[n];
  return dst;
}

void* memset(void* dst, int c, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  asm volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const auto* x = static_cast<const unsigned char*>(a);
  const auto* y = static_cast<const unsigned char*>(b);
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return 0;
}

void* memchr(const void* s, int c, size_t n) {
  const auto* p = static_cast<const unsigned char*>(s);
  for (size_t i = 0; i < n; ++i) {
    if (p[i] == static_cast<unsigned char>(c)) return const_cast<unsigned char*>(p + i);
  }
  return nullptr;
}

size_t strlen(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

size_t strnlen(const char* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0') ++n;
  return n;
}

int strcmp(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) ++a, ++b;
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

int strncmp(const char* a, const char* b, size_t n) {
  for (; n != 0; --n, ++a, ++b) {
    if (*a != *b || *a == '\0') return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
  }
  return 0;
}

int strcasecmp(const char* a, const char* b) {
  while (*a != '\0' && tolower(*a) == tolower(*b)) ++a, ++b;
  return tolower(static_cast<unsigned char>(*a)) - tolower(static_cast<unsigned char>(*b));
}

int strncasecmp(const char* a, const char* b, size_t n) {
  for (; n != 0; --n, ++a, ++b) {
    int x = tolower(static_cast<unsigned char>(*a)), y = tolower(static_cast<unsigned char>(*b));
    if (x != y || x == 0) return x - y;
  }
  return 0;
}

char* strcpy(char* dst, const char* src) {
  char* d = dst;
  while ((*d++ = *src++) != '\0') {
  }
  return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i] != '\0'; ++i) dst[i] = src[i];
  for (; i < n; ++i) dst[i] = '\0';
  return dst;
}

char* strcat(char* dst, const char* src) {
  strcpy(dst + strlen(dst), src);
  return dst;
}

char* strncat(char* dst, const char* src, size_t n) {
  char* d = dst + strlen(dst);
  size_t i = 0;
  for (; i < n && src[i] != 0; ++i) d[i] = src[i];
  d[i] = 0;
  return dst;
}

char* strchr(const char* s, int c) {
  for (;; ++s) {
    if (*s == static_cast<char>(c)) return const_cast<char*>(s);
    if (*s == '\0') return nullptr;
  }
}

char* strrchr(const char* s, int c) {
  const char* last = nullptr;
  for (;; ++s) {
    if (*s == static_cast<char>(c)) last = s;
    if (*s == '\0') return const_cast<char*>(last);
  }
}

char* strstr(const char* h, const char* n) {
  size_t len = strlen(n);
  if (len == 0) return const_cast<char*>(h);
  for (; *h != '\0'; ++h) {
    if (strncmp(h, n, len) == 0) return const_cast<char*>(h);
  }
  return nullptr;
}

size_t strspn(const char* s, const char* accept) {
  size_t n = 0;
  while (s[n] != '\0' && strchr(accept, s[n]) != nullptr) ++n;
  return n;
}

size_t strcspn(const char* s, const char* reject) {
  size_t n = 0;
  while (s[n] != '\0' && strchr(reject, s[n]) == nullptr) ++n;
  return n;
}

char* strtok_r(char* s, const char* delim, char** save) {
  if (s == nullptr) s = *save;
  if (s == nullptr) return nullptr;
  s += strspn(s, delim);
  if (*s == '\0') {
    *save = nullptr;
    return nullptr;
  }
  char* end = s + strcspn(s, delim);
  if (*end != '\0') {
    *end = '\0';
    *save = end + 1;
  } else {
    *save = nullptr;
  }
  return s;
}

char* strtok(char* s, const char* delim) {
  static char* save;
  return strtok_r(s, delim, &save);
}

char* strdup(const char* s) { return strndup(s, strlen(s)); }

char* strndup(const char* s, size_t n) {
  size_t len = strnlen(s, n);
  auto* d = static_cast<char*>(malloc(len + 1));
  if (d == nullptr) return nullptr;
  memcpy(d, s, len);
  d[len] = '\0';
  return d;
}

static const char* error_message(int e) {
  switch (e) {
    case 0: return const_cast<char*>("Success");
    case EPERM: return const_cast<char*>("Operation not permitted");
    case ENOENT: return const_cast<char*>("No such file or directory");
    case ESRCH: return "No such process";
    case E2BIG: return "Argument list too long";
    case ECHILD: return "No child processes";
    case EINTR: return const_cast<char*>("Interrupted");
    case EIO: return const_cast<char*>("I/O error");
    case ENOEXEC: return const_cast<char*>("Exec format error");
    case EBADF: return const_cast<char*>("Bad file descriptor");
    case EAGAIN: return const_cast<char*>("Try again");
    case ENOMEM: return const_cast<char*>("Out of memory");
    case EACCES: return const_cast<char*>("Permission denied");
    case EFAULT: return const_cast<char*>("Bad address");
    case EBUSY: return const_cast<char*>("Busy");
    case EEXIST: return const_cast<char*>("File exists");
    case ENOTDIR: return const_cast<char*>("Not a directory");
    case ENODEV: return const_cast<char*>("No such device");
    case EDEADLK: return const_cast<char*>("Resource deadlock avoided");
    case ENAMETOOLONG: return const_cast<char*>("File name too long");
    case EMSGSIZE: return const_cast<char*>("Message too large");
    case EXDEV: return const_cast<char*>("Cross-device operation");
    case EISDIR: return const_cast<char*>("Is a directory");
    case EINVAL: return const_cast<char*>("Invalid argument");
    case EMFILE: return const_cast<char*>("Too many open files");
    case ENFILE: return const_cast<char*>("Open description table full");
    case ENOTSUP: return const_cast<char*>("Operation not supported");
    case ENOTTY: return "Inappropriate ioctl for device";
    case ENOSPC: return const_cast<char*>("No space left");
    case ESPIPE: return "Illegal seek";
    case EPIPE: return const_cast<char*>("Broken pipe");
    case ERANGE: return "Result out of range";
    case EDOM: return "Numerical argument out of domain";
    case ENOSYS: return const_cast<char*>("Not supported");
    case ENOTEMPTY: return "Directory not empty";
    case ENOTSOCK: return "Not a socket";
    case EILSEQ: return "Invalid byte sequence";
    case EOVERFLOW: return "Value too large";
    case ETIMEDOUT: return const_cast<char*>("Timed out");
    case ECONNREFUSED: return const_cast<char*>("Connection refused");
    case ECONNRESET: return const_cast<char*>("Connection reset");
    case EADDRINUSE: return const_cast<char*>("Address in use");
    case ENETUNREACH: return const_cast<char*>("Network unreachable");
    default: return nullptr;
  }
}

char* strerror(int e) {
  const char* text = error_message(e);
  return const_cast<char*>(text == nullptr ? "Unknown error" : text);
}

int strerror_r(int e, char* buffer, size_t size) {
  const char* text = error_message(e);
  int result = text == nullptr ? EINVAL : 0;
  if (text == nullptr) text = "Unknown error";
  if (size == 0) return ERANGE;
  if (buffer == nullptr) return EINVAL;
  size_t length = strlen(text);
  size_t copied = length < size ? length : size - 1;
  memcpy(buffer, text, copied);
  buffer[copied] = '\0';
  return length < size ? result : ERANGE;
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isprint(int c) { return c >= 0x20 && c < 0x7f; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

}  // extern "C"
