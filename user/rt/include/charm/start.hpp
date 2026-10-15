#pragma once
// Стартовое сообщение "strt" (docs/abi/process.md §2): разбор и сборка.
#include <charm/sys.hpp>

namespace charm {

struct Start {
  abi::process::StartHeader header;
  Handle handles[abi::process::kMaxStartHandles];
  const char* args;  // argc строк с \0 подряд
  const char* env;   // envc строк ИМЯ=значение
  // Хендл с ролью role (первый), 0 — нет.
  Handle find(uint32_t role) const {
    for (uint32_t i = 0; i < header.handle_count; ++i) {
      if (header.roles[i] == role) return handles[i];
    }
    return 0;
  }
};

// buf — не меньше sizeof(StartHeader) + 4096 + 8192 байт.
inline Status read_start(Handle boot, char* buf, uint64_t cap, Start* out) {
  uint32_t nh = 0;
  auto r = channel_read(boot, buf, cap, out->handles, abi::process::kMaxStartHandles, kForever);
  if (!r.ok()) return r.status;
  uint32_t bytes = static_cast<uint32_t>(r.value >> 32);
  nh = static_cast<uint32_t>(r.value);
  if (bytes < sizeof(abi::process::StartHeader)) return Status::InvalidArgs;
  __builtin_memcpy(&out->header, buf, sizeof out->header);
  const auto& h = out->header;
  if (__builtin_memcmp(h.magic, abi::process::kStartMagic, 4) != 0 || h.handle_count != nh ||
      sizeof h + uint64_t{h.args_bytes} + h.env_bytes != bytes) {
    return Status::InvalidArgs;
  }
  out->args = buf + sizeof h;
  out->env = out->args + h.args_bytes;
  return Status::Ok;
}

// i-я строка блока (строки с \0 подряд).
inline const char* nth_string(const char* block, uint32_t i) {
  while (i-- != 0) block += length(block) + 1;
  return block;
}

}  // namespace charm
