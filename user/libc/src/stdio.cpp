// stdio: FILE с буфером поверх дескрипторов, printf.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal.hpp"

struct __file {
  int fd;
  int mode;  // _IOFBF/_IOLBF/_IONBF
  bool eof;
  bool err;
  bool used;
  char wbuf[BUFSIZ];
  size_t wlen;
  char rbuf[BUFSIZ];
  size_t rpos, rlen;
  int unget;
};

namespace libc {
namespace {

constexpr int kMaxFiles = 32;
__file g_files[kMaxFiles];
libc::Lock g_files_lock;

int flush(__file* f) {
  size_t done = 0;
  while (done < f->wlen) {
    ssize_t n = ::write(f->fd, f->wbuf + done, f->wlen - done);
    if (n <= 0) {
      f->err = true;
      f->wlen = 0;
      return EOF;
    }
    done += static_cast<size_t>(n);
  }
  f->wlen = 0;
  return 0;
}

int put(__file* f, char c) {
  if (f->mode == _IONBF) {
    if (::write(f->fd, &c, 1) != 1) {
      f->err = true;
      return EOF;
    }
    return static_cast<unsigned char>(c);
  }
  f->wbuf[f->wlen++] = c;
  if (f->wlen == sizeof f->wbuf || (f->mode == _IOLBF && c == '\n')) {
    if (flush(f) != 0) return EOF;
  }
  return static_cast<unsigned char>(c);
}

__file* new_file(int fd, int mode) {
  Guard g(g_files_lock);
  for (__file& f : g_files) {
    if (!f.used) {
      memset(&f, 0, sizeof f);
      f.used = true;
      f.fd = fd;
      f.mode = mode;
      f.unget = -1;
      return &f;
    }
  }
  return nullptr;
}

// --- форматирование ------------------------------------------------------------------------

struct Out {
  void (*emit)(Out*, char);
  char* buf;
  size_t cap;
  size_t len;  // всего (как вернул бы printf)
  __file* file;
  int fd;
  char tmp[256];
  size_t tlen;
};

void emit_buf(Out* o, char c) {
  if (o->len + 1 < o->cap) o->buf[o->len] = c;
  ++o->len;
}
void emit_file(Out* o, char c) {
  ++o->len;
  put(o->file, c);
}
void emit_fd(Out* o, char c) {
  ++o->len;
  o->tmp[o->tlen++] = c;
  if (o->tlen == sizeof o->tmp) {
    (void)::write(o->fd, o->tmp, o->tlen);
    o->tlen = 0;
  }
}

void pad(Out* o, char c, int n) {
  for (; n > 0; --n) o->emit(o, c);
}

void format(Out* o, const char* fmt, va_list ap) {
  for (; *fmt != '\0'; ++fmt) {
    if (*fmt != '%') {
      o->emit(o, *fmt);
      continue;
    }
    ++fmt;
    bool left = false, zero = false, plus = false, space = false, alt = false;
    for (;; ++fmt) {
      if (*fmt == '-') left = true;
      else if (*fmt == '0') zero = true;
      else if (*fmt == '+') plus = true;
      else if (*fmt == ' ') space = true;
      else if (*fmt == '#') alt = true;
      else break;
    }
    int width = 0, prec = -1;
    if (*fmt == '*') {
      width = va_arg(ap, int);
      if (width < 0) {
        left = true;
        width = -width;
      }
      ++fmt;
    } else {
      while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    }
    if (*fmt == '.') {
      ++fmt;
      prec = 0;
      if (*fmt == '*') {
        prec = va_arg(ap, int);
        ++fmt;
      } else {
        while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
      }
    }
    int lng = 0;  // 1 — l, 2 — ll/z/j
    while (*fmt == 'l' || *fmt == 'z' || *fmt == 'j' || *fmt == 'h' || *fmt == 't') {
      if (*fmt == 'l') ++lng;
      if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') lng = 2;
      ++fmt;
    }
    char c = *fmt;
    if (c == '\0') break;
    char num[72];
    int nlen = 0;
    const char* prefix = "";
    switch (c) {
      case '%':
        o->emit(o, '%');
        continue;
      case 'c': {
        char ch = static_cast<char>(va_arg(ap, int));
        if (!left) pad(o, ' ', width - 1);
        o->emit(o, ch);
        if (left) pad(o, ' ', width - 1);
        continue;
      }
      case 's': {
        const char* s = va_arg(ap, const char*);
        if (s == nullptr) s = "(null)";
        int n = static_cast<int>(prec >= 0 ? strnlen(s, static_cast<size_t>(prec)) : strlen(s));
        if (!left) pad(o, ' ', width - n);
        for (int i = 0; i < n; ++i) o->emit(o, s[i]);
        if (left) pad(o, ' ', width - n);
        continue;
      }
      case 'f': {
        double v = va_arg(ap, double);
        if (prec < 0) prec = 6;
        bool neg = v < 0;
        if (neg) v = -v;
        double scale = 1;
        for (int i = 0; i < prec; ++i) scale *= 10;
        auto whole = static_cast<unsigned long long>(v);
        auto frac = static_cast<unsigned long long>((v - static_cast<double>(whole)) * scale + 0.5);
        if (static_cast<double>(frac) >= scale) {
          ++whole;
          frac = 0;
        }
        char w[32];
        int wl = 0;
        do {
          w[wl++] = static_cast<char>('0' + whole % 10);
          whole /= 10;
        } while (whole != 0);
        if (neg) num[nlen++] = '-';
        while (wl > 0) num[nlen++] = w[--wl];
        if (prec > 0) {
          num[nlen++] = '.';
          for (int i = prec - 1; i >= 0; --i) {
            num[nlen + i] = static_cast<char>('0' + frac % 10);
            frac /= 10;
          }
          nlen += prec;
        }
        if (!left) pad(o, ' ', width - nlen);
        for (int i = 0; i < nlen; ++i) o->emit(o, num[i]);
        if (left) pad(o, ' ', width - nlen);
        continue;
      }
      default:
        break;
    }
    unsigned long long u = 0;
    bool neg = false;
    unsigned base = 10;
    bool upper = false;
    if (c == 'd' || c == 'i') {
      long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
      neg = v < 0;
      u = neg ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
    } else if (c == 'u' || c == 'x' || c == 'X' || c == 'o') {
      u = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
      base = c == 'o' ? 8 : c == 'u' ? 10 : 16;
      upper = c == 'X';
      if (alt && base == 16 && u != 0) prefix = upper ? "0X" : "0x";
    } else if (c == 'p') {
      u = reinterpret_cast<uintptr_t>(va_arg(ap, void*));
      base = 16;
      prefix = "0x";
    } else {
      o->emit(o, '%');
      o->emit(o, c);
      continue;
    }
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char rev[64];
    int rl = 0;
    while (u != 0) {
      rev[rl++] = digits[u % base];
      u /= base;
    }
    if (rl == 0 && prec != 0) rev[rl++] = '0';
    int zeros = prec > rl ? prec - rl : 0;
    const char* sign = neg ? "-" : plus ? "+" : space ? " " : "";
    int total = static_cast<int>(strlen(sign) + strlen(prefix)) + zeros + rl;
    if (zero && !left && prec < 0) {
      zeros += width - total > 0 ? width - total : 0;
      total = width > total ? width : total;
    }
    if (!left) pad(o, ' ', width - total);
    for (const char* s = sign; *s != '\0'; ++s) o->emit(o, *s);
    for (const char* s = prefix; *s != '\0'; ++s) o->emit(o, *s);
    pad(o, '0', zeros);
    while (rl > 0) o->emit(o, rev[--rl]);
    if (left) pad(o, ' ', width - total);
  }
}

}  // namespace

void stdio_init() {
  StartInfo& si = start_info();
  // 0, 1, 2: консоль, если дали; иначе вывод — в журнал ядра (debug_write).
  for (int i = 0; i < 3; ++i) {
    if (si.console != 0) {
      (void)fd_alloc(Kind::Console, si.console, i == 0, i != 0);
    } else {
      (void)fd_alloc(Kind::Debug, 0, i == 0, i != 0);
    }
  }
  stdin = new_file(0, _IOLBF);
  stdout = new_file(1, _IOLBF);
  stderr = new_file(2, _IONBF);
}

void stdio_flush_all() {
  for (__file& f : g_files) {
    if (f.used && f.wlen != 0) (void)flush(&f);
  }
}

}  // namespace libc

using namespace libc;

FILE* stdin = nullptr;
FILE* stdout = nullptr;
FILE* stderr = nullptr;

extern "C" {

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
  Out o{};
  o.emit = &emit_buf;
  o.buf = buf;
  o.cap = n;
  format(&o, fmt, ap);
  if (n != 0) buf[o.len < n ? o.len : n - 1] = '\0';
  return static_cast<int>(o.len);
}

int snprintf(char* buf, size_t n, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, n, fmt, ap);
  va_end(ap);
  return r;
}

int sprintf(char* buf, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, SIZE_MAX, fmt, ap);
  va_end(ap);
  return r;
}

int vfprintf(FILE* f, const char* fmt, va_list ap) {
  Out o{};
  o.emit = &emit_file;
  o.file = f;
  format(&o, fmt, ap);
  return f->err ? -1 : static_cast<int>(o.len);
}

int fprintf(FILE* f, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(f, fmt, ap);
  va_end(ap);
  return r;
}

int vprintf(const char* fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}

int vdprintf(int fd, const char* fmt, va_list ap) {
  Out o{};
  o.emit = &emit_fd;
  o.fd = fd;
  format(&o, fmt, ap);
  if (o.tlen != 0) (void)::write(fd, o.tmp, o.tlen);
  return static_cast<int>(o.len);
}

int dprintf(int fd, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vdprintf(fd, fmt, ap);
  va_end(ap);
  return r;
}

int fputc(int c, FILE* f) { return put(f, static_cast<char>(c)); }
int putc(int c, FILE* f) { return put(f, static_cast<char>(c)); }
int putchar(int c) { return put(stdout, static_cast<char>(c)); }

int fputs(const char* s, FILE* f) {
  for (; *s != '\0'; ++s) {
    if (put(f, *s) == EOF) return EOF;
  }
  return 0;
}

int puts(const char* s) {
  if (fputs(s, stdout) == EOF) return EOF;
  return put(stdout, '\n') == EOF ? EOF : 0;
}

int fflush(FILE* f) {
  if (f == nullptr) {
    stdio_flush_all();
    return 0;
  }
  return flush(f);
}

int fgetc(FILE* f) {
  if (f->unget >= 0) {
    int c = f->unget;
    f->unget = -1;
    return c;
  }
  if (f == stdin) fflush(stdout);  // приглашение до чтения
  if (f->rpos == f->rlen) {
    ssize_t n = ::read(f->fd, f->rbuf, sizeof f->rbuf);
    if (n <= 0) {
      if (n == 0) f->eof = true; else f->err = true;
      return EOF;
    }
    f->rpos = 0;
    f->rlen = static_cast<size_t>(n);
  }
  return static_cast<unsigned char>(f->rbuf[f->rpos++]);
}

int getc(FILE* f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

char* fgets(char* buf, int n, FILE* f) {
  int i = 0;
  while (i < n - 1) {
    int c = fgetc(f);
    if (c == EOF) break;
    buf[i++] = static_cast<char>(c);
    if (c == '\n') break;
  }
  if (i == 0) return nullptr;
  buf[i] = '\0';
  return buf;
}

size_t fread(void* buf, size_t size, size_t n, FILE* f) {
  auto* p = static_cast<char*>(buf);
  size_t total = size * n, got = 0;
  while (got < total) {
    int c = fgetc(f);
    if (c == EOF) break;
    p[got++] = static_cast<char>(c);
  }
  return size != 0 ? got / size : 0;
}

size_t fwrite(const void* buf, size_t size, size_t n, FILE* f) {
  const auto* p = static_cast<const char*>(buf);
  size_t total = size * n, done = 0;
  for (; done < total; ++done) {
    if (put(f, p[done]) == EOF) break;
  }
  return size != 0 ? done / size : 0;
}

FILE* fdopen(int fd, const char*) { return new_file(fd, isatty(fd) ? _IOLBF : _IOFBF); }

FILE* fopen(const char* path, const char* mode) {
  int flags = 0;
  bool plus = strchr(mode, '+') != nullptr;
  switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: errno = EINVAL; return nullptr;
  }
  int fd = open(path, flags);
  if (fd < 0) return nullptr;
  FILE* f = new_file(fd, _IOFBF);
  if (f == nullptr) {
    close(fd);
    errno = EMFILE;
  }
  return f;
}

int fclose(FILE* f) {
  int r = flush(f);
  close(f->fd);
  f->used = false;
  return r;
}

int fseek(FILE* f, long off, int whence) {
  flush(f);
  if (whence == SEEK_CUR) off -= static_cast<long>(f->rlen - f->rpos);
  f->rpos = f->rlen = 0;
  f->unget = -1;
  f->eof = false;
  return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE* f) {
  off_t pos = lseek(f->fd, 0, SEEK_CUR);
  if (pos < 0) return -1;
  return static_cast<long>(pos) - static_cast<long>(f->rlen - f->rpos) + static_cast<long>(f->wlen);
}

int feof(FILE* f) { return f->eof ? 1 : 0; }
int ferror(FILE* f) { return f->err ? 1 : 0; }
void clearerr(FILE* f) { f->eof = f->err = false; }
int fileno(FILE* f) { return f->fd; }

int setvbuf(FILE* f, char*, int mode, size_t) {
  flush(f);
  f->mode = mode;
  return 0;
}

void perror(const char* s) {
  if (s != nullptr && *s != '\0') fprintf(stderr, "%s: %s\n", s, strerror(errno));
  else fprintf(stderr, "%s\n", strerror(errno));
}

}  // extern "C"
