// stdio: FILE с буфером поверх дескрипторов, printf.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "internal.hpp"

struct __file {
  int fd;
  int mode;  // _IOFBF/_IOLBF/_IONBF
  bool readable;
  bool writable;
  bool eof;
  bool err;
  bool used;
  char wbuf[BUFSIZ];
  size_t wlen;
  char rbuf[BUFSIZ];
  size_t rpos, rlen;
  unsigned char unget[8];
  size_t unget_count;
  int orientation;
  mbstate_t wide_state;
};
static_assert(sizeof(fpos_t)==24 && __builtin_offsetof(fpos_t,state)==8);
extern "C" int __charm_numeric_vsnprintf(char*,size_t,const char*,va_list) __attribute__((weak));
extern "C" int __charm_numeric_vswprintf(wchar_t*,size_t,const wchar_t*,va_list) __attribute__((weak));

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
      if (n == 0) errno = EIO;
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
  if (!f->writable || f->err) {
    if (!f->writable) errno = EBADF;
    f->err = true;
    return EOF;
  }
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

__file* new_file(int fd, int mode, bool readable, bool writable) {
  Guard g(g_files_lock);
  for (__file& f : g_files) {
    if (!f.used) {
      memset(&f, 0, sizeof f);
      f.used = true;
      f.fd = fd;
      f.mode = mode;
      f.readable = readable;
      f.writable = writable;
      return &f;
    }
  }
  errno = EMFILE;
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
  bool err;
  int err_no;
};

void emit_buf(Out* o, char c);

bool write_all(int fd, const char* data, size_t size) {
  size_t done = 0;
  while (done < size) {
    ssize_t n = ::write(fd, data + done, size - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) {
      errno = EIO;
      return false;
    }
    done += static_cast<size_t>(n);
  }
  return true;
}

void emit_repeat(Out* o, char c, size_t n) {
  if (o->emit == &emit_buf) {
    size_t room = o->len + 1 < o->cap ? o->cap - o->len - 1 : 0;
    size_t take = n < room ? n : room;
    if (o->buf != nullptr && take != 0) memset(o->buf + o->len, c, take);
    o->len += n;
    return;
  }
  if (o->err) return;
  for (size_t i = 0; i < n; ++i) o->emit(o, c);
}

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
  if (o->err) return;
  o->tmp[o->tlen++] = c;
  if (o->tlen == sizeof o->tmp) {
    if (!write_all(o->fd, o->tmp, o->tlen)) {
      o->err = true;
      o->err_no = errno;
    }
    o->tlen = 0;
  }
}

void pad(Out* o, char c, int n) {
  if (n > 0) emit_repeat(o, c, static_cast<size_t>(n));
}

// Exact binary-to-decimal conversion for %f. A double is mant * 2^exp.
// For exp < 0 the value is mant * 5^-exp / 10^-exp, so a small base-1e9
// integer is enough to obtain every decimal digit without a fixed output
// buffer (the old num[72] was overflowed by large precisions).
struct Big {
  static constexpr uint32_t kBase = 1000000000u;
  uint32_t limb[160];
  int count = 0;

  void set(uint64_t value) {
    do {
      limb[count++] = static_cast<uint32_t>(value % kBase);
      value /= kBase;
    } while (value != 0);
  }
  void mul(uint32_t factor) {
    uint64_t carry = 0;
    for (int i = 0; i < count; ++i) {
      uint64_t value = static_cast<uint64_t>(limb[i]) * factor + carry;
      limb[i] = static_cast<uint32_t>(value % kBase);
      carry = value / kBase;
    }
    while (carry != 0) {
      limb[count++] = static_cast<uint32_t>(carry % kBase);
      carry /= kBase;
    }
  }
  void mul_pow2(int exponent) {
    while (exponent-- > 0) mul(2);
  }
  void mul_pow5(int exponent) {
    while (exponent-- > 0) mul(5);
  }
  int digits(char* out, int capacity) const {
    int pos = 0;
    auto write = [&](uint32_t value, int width) {
      char reverse[10];
      int n = 0;
      do {
        reverse[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
      } while (value != 0);
      while (n < width) reverse[n++] = '0';
      while (n > 0) {
        char c = reverse[--n];
        if (pos < capacity) out[pos] = c;
        ++pos;
      }
    };
    write(limb[count - 1], 0);
    for (int i = count - 2; i >= 0; --i) write(limb[i], 9);
    return pos;
  }
};

void emit_special(Out* o, bool negative, const char* text, int width, bool left,
                  bool zero, bool plus, bool space) {
  (void)zero;
  const int sign_len = (negative || plus || space) ? 1 : 0;
  const int total = sign_len + static_cast<int>(strlen(text));
  if (!left) pad(o, ' ', width - total);
  if (negative) o->emit(o, '-');
  else if (plus) o->emit(o, '+');
  else if (space) o->emit(o, ' ');
  for (const char* p = text; *p; ++p) o->emit(o, *p);
  if (left) pad(o, ' ', width - total);
}

void format_double(Out* o, double value, int prec, int width, bool left,
                   bool zero, bool plus, bool space, bool alt) {
  const bool negative = __builtin_signbit(value) != 0;
  if (__builtin_isnan(value)) {
    emit_special(o, false, "nan", width, left, zero, plus, space);
    return;
  }
  if (__builtin_isinf(value)) {
    emit_special(o, negative, "inf", width, left, zero, plus, space);
    return;
  }
  if (prec < 0) prec = 6;

  uint64_t bits;
  memcpy(&bits, &value, sizeof bits);
  const int biased_exp = static_cast<int>((bits >> 52) & 0x7ff);
  const uint64_t mantissa = bits & ((uint64_t{1} << 52) - 1);
  char all[1400];
  int len = 0;
  int point = 0;
  if (biased_exp == 0 && mantissa == 0) {
    all[len++] = '0';
    point = 1;
  } else {
    Big b;
    if (biased_exp == 0) {
      b.set(mantissa);
      b.mul_pow5(1074);
      len = b.digits(all, sizeof all);
      point = len - 1074;
    } else {
      const uint64_t normal = mantissa | (uint64_t{1} << 52);
      const int exponent = biased_exp - 1023 - 52;
      b.set(normal);
      if (exponent >= 0) {
        b.mul_pow2(exponent);
        len = b.digits(all, sizeof all);
        point = len;
      } else {
        b.mul_pow5(-exponent);
        len = b.digits(all, sizeof all);
        point = len + exponent;
      }
    }
    if (point <= 0) {
      const int leading = -point;
      if (leading + len < static_cast<int>(sizeof all)) {
        memmove(all + leading, all, static_cast<size_t>(len));
        memset(all, '0', static_cast<size_t>(leading));
        len += leading;
        point = 0;
      }
    } else if (point > len) {
      if (point < static_cast<int>(sizeof all)) {
        memset(all + len, '0', static_cast<size_t>(point - len));
        len = point;
      }
    }
    if (point == 0) {
      memmove(all + 1, all, static_cast<size_t>(len));
      all[0] = '0';
      ++len;
      point = 1;
    }
  }

  const long long target = static_cast<long long>(point) + prec;
  if (target < len) {
    const char next = all[target];
    bool nonzero_tail = false;
    for (int i = static_cast<int>(target) + 1; i < len; ++i) {
      if (all[i] != '0') {
        nonzero_tail = true;
        break;
      }
    }
    const bool round_up =
        next > '5' ||
        (next == '5' &&
         (nonzero_tail || ((all[target - 1] - '0') & 1) != 0));
    len = static_cast<int>(target);
    if (round_up) {
      int i = len - 1;
      while (i >= 0 && all[i] == '9') all[i--] = '0';
      if (i >= 0) {
        ++all[i];
      } else if (len + 1 < static_cast<int>(sizeof all)) {
        memmove(all + 1, all, static_cast<size_t>(len));
        all[0] = '1';
        ++len;
        ++point;
      }
    }
  }

  const int sign_len = (negative || plus || space) ? 1 : 0;
  const long long total =
      sign_len + point + (prec > 0 ? 1 + static_cast<long long>(prec)
                                   : (alt ? 1 : 0));
  if (!left && !zero && width > total)
    pad(o, ' ', static_cast<int>(width - total));
  if (negative) o->emit(o, '-');
  else if (plus) o->emit(o, '+');
  else if (space) o->emit(o, ' ');
  if (zero && !left && width > total)
    pad(o, '0', static_cast<int>(width - total));
  for (int i = 0; i < point; ++i) o->emit(o, all[i]);
  if (prec > 0 || alt) o->emit(o, '.');
  int available = len > point ? len - point : 0;
  if (available > prec) available = prec;
  for (int i = 0; i < available; ++i) o->emit(o, all[point + i]);
  if (prec > available) pad(o, '0', prec - available);
  if (left && width > total) pad(o, ' ', static_cast<int>(width - total));
}

int read_decimal(const char** pp) {
  int value = 0;
  const char* p = *pp;
  while (*p >= '0' && *p <= '9') {
    const int digit = *p - '0';
    if (value > (INT_MAX - digit) / 10) {
      value = INT_MAX;
    } else {
      value = value * 10 + digit;
    }
    ++p;
  }
  *pp = p;
  return value;
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
        width = width == INT_MIN ? INT_MAX : -width;
      }
      ++fmt;
    } else {
      width = read_decimal(&fmt);
    }
    if (*fmt == '.') {
      ++fmt;
      if (*fmt == '*') {
        const int value = va_arg(ap, int);
        prec = value < 0 ? -1 : value;
        ++fmt;
      } else {
        prec = read_decimal(&fmt);
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
        format_double(o, va_arg(ap, double), prec, width, left, zero, plus,
                      space, alt);
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
  stdin = new_file(0, _IOLBF, true, false);
  stdout = new_file(1, _IOLBF, false, true);
  stderr = new_file(2, _IONBF, false, true);
}

int stdio_flush_all() {
  int result = 0;
  int saved_errno = 0;
  for (__file& f : g_files) {
    if (!f.used) continue;
    if (flush(&f) != 0) {
      if (result == 0) saved_errno = errno;
      result = EOF;
    }
  }
  if (result != 0) errno = saved_errno;
  return result;
}

}  // namespace libc

using namespace libc;

FILE* stdin = nullptr;
FILE* stdout = nullptr;
FILE* stderr = nullptr;

extern "C" {

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
  if(__charm_numeric_vsnprintf) return __charm_numeric_vsnprintf(buf,n,fmt,ap);
  Out o{};
  o.emit = &emit_buf;
  o.buf = buf;
  o.cap = n;
  format(&o, fmt, ap);
  if (o.len > static_cast<size_t>(INT_MAX)) {
    errno = EOVERFLOW;
    if (n != 0) buf[0] = '\0';
    return -1;
  }
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
  if(fwide(f,-1)>0) { errno=EINVAL; f->err=true; return -1; }
  if(__charm_numeric_vsnprintf) {
    char* buffer=nullptr;
    int length=vasprintf(&buffer,fmt,ap);
    if(length<0) return -1;
    size_t written=fwrite(buffer,1,static_cast<size_t>(length),f);
    free(buffer);
    return written==static_cast<size_t>(length) ? length : -1;
  }
  Out o{};
  o.emit = &emit_file;
  o.file = f;
  format(&o, fmt, ap);
  if (o.len > static_cast<size_t>(INT_MAX)) {
    errno = EOVERFLOW;
    f->err = true;
    return -1;
  }
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
  if(__charm_numeric_vsnprintf) {
    char* buffer=nullptr;
    int length=vasprintf(&buffer,fmt,ap);
    if(length<0) return -1;
    if (!write_all(fd, buffer, static_cast<size_t>(length))) {
      const int saved_errno = errno;
      free(buffer);
      errno = saved_errno;
      return -1;
    }
    free(buffer); return length;
  }
  Out o{};
  o.emit = &emit_fd;
  o.fd = fd;
  format(&o, fmt, ap);
  if (o.tlen != 0 && !o.err && !write_all(fd, o.tmp, o.tlen)) {
    o.err = true;
    o.err_no = errno;
  }
  o.tlen = 0;
  if (o.err) {
    errno = o.err_no;
    return -1;
  }
  if (o.len > static_cast<size_t>(INT_MAX)) {
    errno = EOVERFLOW;
    return -1;
  }
  return static_cast<int>(o.len);
}

int dprintf(int fd, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vdprintf(fd, fmt, ap);
  va_end(ap);
  return r;
}

int fputc(int c, FILE* f) {
  if(fwide(f,-1)>0) { errno=EINVAL; f->err=true; return EOF; }
  return put(f,static_cast<char>(c));
}
int putc(int c, FILE* f) { return fputc(c,f); }
int putchar(int c) { return fputc(c,stdout); }

int fputs(const char* s, FILE* f) {
  if(fwide(f,-1)>0) { errno=EINVAL; f->err=true; return EOF; }
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
  if (f == nullptr) return stdio_flush_all();
  return flush(f);
}

static int get_byte(FILE* f) {
  if (!f->readable) {
    errno = EBADF;
    f->err = true;
    return EOF;
  }
  if(f->unget_count) return f->unget[--f->unget_count];
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
int fgetc(FILE* f) {
  if(fwide(f,-1)>0) { errno=EINVAL; f->err=true; return EOF; }
  return get_byte(f);
}
int ungetc(int c,FILE* f) {
  if(c==EOF || f->unget_count==sizeof f->unget || fwide(f,-1)>0) return EOF;
  f->unget[f->unget_count++]=static_cast<unsigned char>(c); f->eof=false;
  return static_cast<unsigned char>(c);
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
  if(!size || !n) return 0;
  if(size && n>SIZE_MAX/size) { errno=EOVERFLOW; f->err=true; return 0; }
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
  if(!size || !n) return 0;
  if(size && n>SIZE_MAX/size) { errno=EOVERFLOW; f->err=true; return 0; }
  if(fwide(f,-1)>0) { errno=EINVAL; f->err=true; return 0; }
  const auto* p = static_cast<const char*>(buf);
  size_t total = size * n, done = 0;
  for (; done < total; ++done) {
    if (put(f, p[done]) == EOF) break;
  }
  return size != 0 ? done / size : 0;
}

FILE* fdopen(int fd, const char* mode) {
  if (mode == nullptr || mode[0] == '\0') {
    errno = EINVAL;
    return nullptr;
  }
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0) return nullptr;  // EBADF, descriptor stays open

  int access = O_RDONLY;
  bool append = false;
  switch (mode[0]) {
    case 'r': access = O_RDONLY; break;
    case 'w': access = O_WRONLY; break;
    case 'a': access = O_WRONLY; append = true; break;
    default: errno = EINVAL; return nullptr;
  }
  if (strchr(mode, '+') != nullptr) access = O_RDWR;
  for (const char* p = mode + 1; *p != '\0'; ++p) {
    if (*p != '+' && *p != 'b' && *p != 'e') {
      errno = EINVAL;
      return nullptr;
    }
  }

  const int descriptor_access = flags & O_ACCMODE;
  const bool can_read = descriptor_access != O_WRONLY;
  const bool can_write = descriptor_access != O_RDONLY;
  if ((access == O_RDONLY && !can_read) ||
      (access == O_WRONLY && !can_write) ||
      (access == O_RDWR && !(can_read && can_write))) {
    errno = EINVAL;
    return nullptr;
  }
  if (append && fcntl(fd, F_SETFL, flags | O_APPEND) < 0) return nullptr;
  if (strchr(mode, 'e') != nullptr && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
    return nullptr;

  return new_file(fd, isatty(fd) ? _IOLBF : _IOFBF, access != O_WRONLY,
                  access != O_RDONLY);
}

FILE* fopen(const char* path, const char* mode) {
  if(!path || !mode) { errno=EFAULT; return nullptr; }
  int flags = 0;
  bool plus = strchr(mode, '+') != nullptr;
  switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: errno = EINVAL; return nullptr;
  }
  if(strchr(mode,'e')) flags|=O_CLOEXEC;
  if(strchr(mode,'x')) {
    if(!(flags&O_CREAT)) { errno=EINVAL; return nullptr; }
    flags|=O_EXCL;
  }
  int fd = open(path, flags);
  if (fd < 0) return nullptr;
  const int access = flags & O_ACCMODE;
  FILE* f = new_file(fd, _IOFBF, access != O_WRONLY, access != O_RDONLY);
  if (f == nullptr) {
    const int saved_errno = errno;
    close(fd);
    errno = saved_errno;
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
  if(flush(f)) return -1;
  if (whence == SEEK_CUR && __builtin_sub_overflow(off,static_cast<long>(f->rlen-f->rpos+f->unget_count),&off)) {
    errno=EOVERFLOW; return -1;
  }
  if(lseek(f->fd,off,whence)<0) return -1;
  f->rpos = f->rlen = 0;
  f->unget_count = 0;
  f->wide_state = {};
  f->eof = false;
  return 0;
}

long ftell(FILE* f) {
  off_t pos = lseek(f->fd, 0, SEEK_CUR);
  if (pos < 0) return -1;
  long result;
  if(__builtin_sub_overflow(static_cast<long>(pos),static_cast<long>(f->rlen-f->rpos+f->unget_count),&result) ||
     __builtin_add_overflow(result,static_cast<long>(f->wlen),&result)) { errno=EOVERFLOW; return -1; }
  return result;
}

int fseeko(FILE* f,off_t off,int whence) { return fseek(f,off,whence); }
off_t ftello(FILE* f) { return ftell(f); }
int fgetpos(FILE* f,fpos_t* out) {
  if(!out) { errno=EINVAL; return -1; }
  off_t offset=ftello(f); if(offset<0) return -1;
  *out={offset,f->wide_state}; return 0;
}
int fsetpos(FILE* f,const fpos_t* position) {
  if(!position) { errno=EINVAL; return -1; }
  if(fseeko(f,position->offset,SEEK_SET)) return -1;
  f->wide_state=position->state; return 0;
}
void rewind(FILE* f) { (void)fseek(f,0,SEEK_SET); clearerr(f); }
void setbuf(FILE* f,char* buffer) { (void)setvbuf(f,buffer,buffer?_IOFBF:_IONBF,BUFSIZ); }
int fwide(FILE* f,int mode) {
  if(!f->orientation && mode) f->orientation=mode>0?1:-1;
  return f->orientation;
}
wint_t fgetwc(FILE* f) {
  if(fwide(f,1)<0) { errno=EINVAL; f->err=true; return WEOF; }
  for(;;) {
    int c=get_byte(f);
    if(c==EOF) {
      if(!mbsinit(&f->wide_state)) { f->wide_state={}; errno=EILSEQ; f->err=true; }
      return WEOF;
    }
    char byte=static_cast<char>(c); wchar_t value;
    size_t result=mbrtowc(&value,&byte,1,&f->wide_state);
    if(result==static_cast<size_t>(-1)) { f->err=true; return WEOF; }
    if(result!=static_cast<size_t>(-2)) return static_cast<wint_t>(value);
  }
}
wint_t getwc(FILE* f) { return fgetwc(f); }
wint_t getwchar(void) { return fgetwc(stdin); }
wint_t fputwc(wchar_t c,FILE* f) {
  if(fwide(f,1)<0) { errno=EINVAL; f->err=true; return WEOF; }
  char bytes[4]; size_t n=wcrtomb(bytes,c,&f->wide_state);
  if(n==static_cast<size_t>(-1)) { f->err=true; return WEOF; }
  for(size_t i=0;i<n;++i) if(put(f,bytes[i])==EOF) return WEOF;
  return static_cast<wint_t>(c);
}
wint_t putwc(wchar_t c,FILE* f) { return fputwc(c,f); }
wint_t putwchar(wchar_t c) { return fputwc(c,stdout); }
wint_t ungetwc(wint_t c,FILE* f) {
  if(c==WEOF || fwide(f,1)<0) return WEOF;
  char bytes[4]; mbstate_t state{}; size_t n=wcrtomb(bytes,static_cast<wchar_t>(c),&state);
  if(n==static_cast<size_t>(-1) || n>sizeof f->unget-f->unget_count) return WEOF;
  while(n) f->unget[f->unget_count++]=static_cast<unsigned char>(bytes[--n]);
  f->wide_state={}; f->eof=false; return c;
}
int vasprintf(char** out,const char* format_string,va_list args) {
  if(!out) { errno=EINVAL; return -1; }
  *out=nullptr; va_list count_args; va_copy(count_args,args);
  int count=vsnprintf(nullptr,0,format_string,count_args); va_end(count_args);
  if(count<0) return -1;
  auto* buffer=static_cast<char*>(malloc(static_cast<size_t>(count)+1));
  if(!buffer) return -1;
  va_list write_args; va_copy(write_args,args);
  int written=vsnprintf(buffer,static_cast<size_t>(count)+1,format_string,write_args); va_end(write_args);
  if(written!=count) { free(buffer); errno=EOVERFLOW; return -1; }
  *out=buffer; return count;
}
int asprintf(char** out,const char* format_string,...) {
  va_list args; va_start(args,format_string);
  int result=vasprintf(out,format_string,args); va_end(args); return result;
}
int vswprintf(wchar_t* out,size_t n,const wchar_t* format_string,va_list args) {
  if(__charm_numeric_vswprintf) return __charm_numeric_vswprintf(out,n,format_string,args);
  errno=ENOSYS; return -1;
}
int swprintf(wchar_t* out,size_t n,const wchar_t* format_string,...) {
  va_list args; va_start(args,format_string);
  int result=vswprintf(out,n,format_string,args); va_end(args); return result;
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

FILE* freopen(const char* path, const char* mode, FILE* stream) {
  if (stream != nullptr) fclose(stream);
  return fopen(path, mode);
}

}  // extern "C"
