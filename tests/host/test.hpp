#pragma once
// Минимальный фреймворк хост-тестов: TEST регистрирует функцию,
// CHECK* печатают место провала и помечают тест упавшим.

#include <cstdio>
#include <cstring>

namespace test {

struct Case {
  const char* name;
  void (*fn)();
  Case* next;
};

inline Case* g_head = nullptr;
inline int g_failures = 0;

struct Registrar {
  Case c;
  Registrar(const char* name, void (*fn)()) : c{name, fn, g_head} { g_head = &c; }
};

inline void fail(const char* file, int line, const char* expr) {
  std::printf("  FAIL %s:%d: %s\n", file, line, expr);
  ++g_failures;
}

}  // namespace test

#define TEST(name)                                         \
  static void test_##name();                               \
  static test::Registrar reg_##name(#name, &test_##name);  \
  static void test_##name()

#define CHECK(expr) \
  do { if (!(expr)) test::fail(__FILE__, __LINE__, #expr); } while (0)

#define CHECK_STR(buf, len, expected)                                     \
  CHECK((len) == std::strlen(expected) &&                                 \
        std::memcmp((buf), (expected), std::strlen(expected)) == 0)

int main() {
  int total = 0;
  for (test::Case* c = test::g_head; c != nullptr; c = c->next) {
    int before = test::g_failures;
    c->fn();
    std::printf("%s %s\n", test::g_failures == before ? "ok  " : "FAIL", c->name);
    ++total;
  }
  std::printf("%d tests, %d failed checks\n", total, test::g_failures);
  return test::g_failures == 0 ? 0 : 1;
}
