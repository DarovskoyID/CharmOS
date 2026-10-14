#pragma once
// Проверки в тестовых программах: при провале печатает строку и номер
// проверки, а процесс завершается с этим номером (кодом выхода).
#include <charm/sys.hpp>
#include <lib/core/fmt.hpp>

#define EXPECT(cond)                                                                     \
  do {                                                                                   \
    if (!(cond)) {                                                                       \
      char buf_[160];                                                                    \
      auto s_ = lib::core::format_to(buf_, sizeof buf_, "FAILED line {}: {}\n", __LINE__, \
                                     #cond);                                             \
      charm::debug_write(s_.data(), s_.size());                                          \
      return __LINE__;                                                                   \
    }                                                                                    \
  } while (0)

namespace charm {
using abi::syscall::Status;

// Чтение одного сообщения (ждать бесконечно) в буфер; возвращает статус.
inline Status read_msg(Handle ch, char* buf, uint64_t cap, Handle* hs, uint64_t hcap,
                       uint32_t* bytes, uint32_t* nh) {
  auto r = channel_read(ch, buf, cap, hs, hcap, kForever);
  *bytes = static_cast<uint32_t>(r.value >> 32);
  *nh = static_cast<uint32_t>(r.value);
  return r.status;
}

inline bool same(const char* a, uint32_t n, const char* b) {
  if (length(b) != n) return false;
  for (uint32_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

inline Status send(Handle ch, const char* text, const Handle* hs = nullptr, uint64_t n = 0) {
  return channel_write(ch, text, length(text), hs, n);
}
}  // namespace charm
