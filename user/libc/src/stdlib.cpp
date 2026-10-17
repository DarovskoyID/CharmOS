// Куча (malloc поверх VMO), числа, окружение, ошибки, блокировки.
//
// Куча: классы по степеням двойки от 16 байт до 64 КиБ — свободные блоки в
// списке своего класса, память — кусками по 1 МиБ из VMO. Больше 64 КиБ —
// своё отображение VMO, free его снимает. Правило 9: без оптимизаций.
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal.hpp"
static_assert(sizeof(div_t) == 8 && __builtin_offsetof(div_t, rem) == 4);
static_assert(sizeof(ldiv_t) == 16 && __builtin_offsetof(ldiv_t, rem) == 8);
static_assert(sizeof(lldiv_t) == 16 && __builtin_offsetof(lldiv_t, rem) == 8);

namespace libc {

void Lock::lock() {
  uint32_t c = __sync_val_compare_and_swap(&state, 0u, 1u);
  if (c == 0) return;
  if (c != 2) c = __atomic_exchange_n(&state, 2u, __ATOMIC_ACQUIRE);
  while (c != 0) {
    (void)charm::futex_wait(&state, 2, charm::kForever);
    c = __atomic_exchange_n(&state, 2u, __ATOMIC_ACQUIRE);
  }
}

void Lock::unlock() {
  if (__atomic_exchange_n(&state, 0u, __ATOMIC_RELEASE) == 2) (void)charm::futex_wake(&state, 1);
}

int errno_from(Status s) {
  switch (s) {
    case Status::Ok: return 0;
    case Status::NotSupported: return ENOSYS;
    case Status::InvalidArgs: return EINVAL;
    case Status::BadAddress: return EFAULT;
    case Status::NoMemory: return ENOMEM;
    case Status::BadHandle: return EBADF;
    case Status::AccessDenied: return EACCES;
    case Status::WrongType: return ENOTDIR;
    case Status::WouldBlock: return EAGAIN;
    case Status::TimedOut: return ETIMEDOUT;
    case Status::PeerClosed: return EPIPE;
    case Status::OutOfRange: return ERANGE;
    case Status::BufferTooSmall: return ERANGE;
    case Status::Busy: return EBUSY;
    case Status::IoError: return EIO;
    case Status::NotFound: return ENOENT;
    case Status::AlreadyExists: return EEXIST;
    case Status::NoSpace: return ENOSPC;
    case Status::ConnectionRefused: return ECONNREFUSED;
    case Status::ConnectionReset: return ECONNRESET;
    case Status::NetUnreachable: return ENETUNREACH;
    case Status::AddressInUse: return EADDRINUSE;
    case Status::Interrupted: return EINTR;
  }
  return EIO;
}

ThreadBlock* current_thread() {
  ThreadBlock* t;
  asm("mov %%fs:0, %0" : "=r"(t));
  return t;
}

namespace {

constexpr uint32_t kMinShift = 4;    // 16 байт
constexpr uint32_t kMaxShift = 16;   // 64 КиБ
constexpr uint64_t kChunk = 1u << 20;
constexpr uint64_t kBigMagic = 0x42494721u;

// Заголовок перед каждым блоком (16 байт: выравнивание данных на 16).
struct Header {
  uint32_t shift;  // класс; 0 — большой блок
  uint32_t magic;
  uint64_t size;   // для большого — размер отображения
};
static_assert(sizeof(Header) == 16);

struct FreeBlock {
  FreeBlock* next;
};

Lock g_heap_lock;
FreeBlock* g_free[kMaxShift + 1] = {};
char* g_chunk_cur = nullptr;
char* g_chunk_end = nullptr;

void* map_vmo(uint64_t size) {
  auto v = charm::vmo_create(size);
  if (!v.ok()) return nullptr;
  auto m = charm::vmo_map(static_cast<Handle>(v.value), charm::rights::kRead | charm::rights::kWrite);
  (void)charm::handle_close(static_cast<Handle>(v.value));  // отображение держит объект
  return m.ok() ? reinterpret_cast<void*>(m.value) : nullptr;
}

uint32_t class_of(size_t n) {
  uint32_t s = kMinShift;
  while ((size_t{1} << s) < n + sizeof(Header)) ++s;
  return s;
}

}  // namespace

}  // namespace libc

using namespace libc;

extern "C" {

int* __errno_location(void) { return &current_thread()->err; }

void* malloc(size_t n) {
  if (n == 0) n = 1;
  if (n > SIZE_MAX - sizeof(Header) - 4095) { errno = ENOMEM; return nullptr; }
  if (n > (size_t{1} << kMaxShift) - sizeof(Header)) {
    uint64_t size = (n + sizeof(Header) + 4095) & ~uint64_t{4095};
    auto* h = static_cast<Header*>(map_vmo(size));
    if (h == nullptr) {
      errno = ENOMEM;
      return nullptr;
    }
    *h = {0, kBigMagic, size};
    return h + 1;
  }
  uint32_t s = class_of(n);
  Guard g(g_heap_lock);
  if (FreeBlock* b = g_free[s]) {
    g_free[s] = b->next;
    auto* h = reinterpret_cast<Header*>(b) - 1;
    return h + 1;
  }
  size_t block = size_t{1} << s;
  if (g_chunk_cur == nullptr || static_cast<size_t>(g_chunk_end - g_chunk_cur) < block) {
    auto* c = static_cast<char*>(map_vmo(kChunk));
    if (c == nullptr) {
      errno = ENOMEM;
      return nullptr;
    }
    g_chunk_cur = c;
    g_chunk_end = c + kChunk;
  }
  auto* h = reinterpret_cast<Header*>(g_chunk_cur);
  g_chunk_cur += block;
  *h = {s, 0, block};
  return h + 1;
}

void free(void* p) {
  if (p == nullptr) return;
  auto* h = static_cast<Header*>(p) - 1;
  if (h->shift == 0) {
    if (h->magic == kBigMagic) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(h));
    return;
  }
  Guard g(g_heap_lock);
  auto* b = static_cast<FreeBlock*>(p);
  b->next = g_free[h->shift];
  g_free[h->shift] = b;
}

void* calloc(size_t n, size_t size) {
  if (size != 0 && n > SIZE_MAX / size) {
    errno = ENOMEM;
    return nullptr;
  }
  void* p = malloc(n * size);
  if (p != nullptr) memset(p, 0, n * size);
  return p;
}

void* realloc(void* p, size_t n) {
  if (p == nullptr) return malloc(n);
  if (n == 0) {
    free(p);
    return nullptr;
  }
  auto* h = static_cast<Header*>(p) - 1;
  size_t have = h->size - sizeof(Header);
  if (n <= have) return p;
  void* q = malloc(n);
  if (q == nullptr) return nullptr;
  memcpy(q, p, have);
  free(p);
  return q;
}

// --- числа --------------------------------------------------------------------------------

unsigned long long strtoull(const char* s, char** end, int base) {
  const char* p = s;
  while (isspace(static_cast<unsigned char>(*p))) ++p;
  bool neg = false;
  if (*p == '+' || *p == '-') neg = *p++ == '-';
  if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit(p[2])) {
    p += 2;
    base = 16;
  } else if (base == 0 && p[0] == '0') {
    base = 8;
  } else if (base == 0) {
    base = 10;
  }
  unsigned long long v = 0;
  const char* digits = p;
  for (;; ++p) {
    int d;
    if (isdigit(static_cast<unsigned char>(*p))) {
      d = *p - '0';
    } else if (isalpha(static_cast<unsigned char>(*p))) {
      d = tolower(*p) - 'a' + 10;
    } else {
      break;
    }
    if (d >= base) break;
    v = v * static_cast<unsigned>(base) + static_cast<unsigned>(d);
  }
  if (end != nullptr) *end = const_cast<char*>(p == digits ? s : p);
  return neg ? 0 - v : v;
}

void* bsearch(const void* key, const void* base, size_t n, size_t size, int (*cmp)(const void*, const void*)) {
  const auto* bytes = static_cast<const unsigned char*>(base);
  if (!size || n > static_cast<size_t>(-1) / size) return nullptr;
  while (n) {
    size_t middle = n / 2;
    const void* item = bytes + middle * size;
    int order = cmp(key, item);
    if (!order) return const_cast<void*>(item);
    if (order < 0) n = middle;
    else { bytes += (middle + 1) * size; n -= middle + 1; }
  }
  return nullptr;
}
long long strtoll(const char* s, char** end, int base) { return static_cast<long long>(strtoull(s, end, base)); }
long strtol(const char* s, char** end, int base) { return static_cast<long>(strtoull(s, end, base)); }
unsigned long strtoul(const char* s, char** end, int base) { return strtoull(s, end, base); }
int atoi(const char* s) { return static_cast<int>(strtol(s, nullptr, 10)); }
long atol(const char* s) { return strtol(s, nullptr, 10); }
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
div_t div(int numerator, int denominator) { return {numerator / denominator, numerator % denominator}; }
ldiv_t ldiv(long numerator, long denominator) { return {numerator / denominator, numerator % denominator}; }
lldiv_t lldiv(long long numerator, long long denominator) { return {numerator / denominator, numerator % denominator}; }

static unsigned long g_rand = 1;
int rand(void) {
  g_rand = g_rand * 1103515245 + 12345;
  return static_cast<int>((g_rand >> 1) & RAND_MAX);
}
void srand(unsigned seed) { g_rand = seed; }

void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*)) {
  // Вставками: списки в утилитах короткие (правило 9).
  auto* b = static_cast<char*>(base);
  char tmp[256];
  if (size > sizeof tmp) return;
  for (size_t i = 1; i < n; ++i) {
    memcpy(tmp, b + i * size, size);
    size_t j = i;
    while (j > 0 && cmp(b + (j - 1) * size, tmp) > 0) {
      memcpy(b + j * size, b + (j - 1) * size, size);
      --j;
    }
    memcpy(b + j * size, tmp, size);
  }
}

// --- окружение -----------------------------------------------------------------------------

static size_t name_len(const char* entry) {
  const char* eq = strchr(entry, '=');
  return eq != nullptr ? static_cast<size_t>(eq - entry) : strlen(entry);
}

char* getenv(const char* name) {
  size_t n = strlen(name);
  for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
    if (name_len(*e) == n && strncmp(*e, name, n) == 0) return *e + n + 1;
  }
  return nullptr;
}

int unsetenv(const char* name) {
  size_t n = strlen(name);
  if (environ == nullptr) return 0;
  char** w = environ;
  for (char** e = environ; *e != nullptr; ++e) {
    if (name_len(*e) == n && strncmp(*e, name, n) == 0) {
      free(*e);
    } else {
      *w++ = *e;
    }
  }
  *w = nullptr;
  return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
  if (name[0] == '\0' || strchr(name, '=') != nullptr) {
    errno = EINVAL;
    return -1;
  }
  if (getenv(name) != nullptr) {
    if (!overwrite) return 0;
    unsetenv(name);
  }
  size_t count = 0;
  while (environ != nullptr && environ[count] != nullptr) ++count;
  auto** e = static_cast<char**>(realloc(environ, (count + 2) * sizeof(char*)));
  if (e == nullptr) return -1;
  size_t ln = strlen(name), lv = strlen(value);
  auto* s = static_cast<char*>(malloc(ln + lv + 2));
  if (s == nullptr) return -1;
  memcpy(s, name, ln);
  s[ln] = '=';
  memcpy(s + ln + 1, value, lv + 1);
  e[count] = s;
  e[count + 1] = nullptr;
  environ = e;
  return 0;
}

}  // extern "C"
