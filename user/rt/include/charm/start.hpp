#pragma once
// Стартовое сообщение "strt" (docs/abi/process.md §2): разбор и сборка.
#include <charm/sys.hpp>

namespace charm {

struct Start {
  abi::process::StartHeaderV2 header;
  Handle handles[abi::process::kMaxStartHandlesV2];
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

// buf — не меньше sizeof(StartHeaderV2) + 4096 + 8192 байт.
inline Status read_start(Handle boot, char* buf, uint64_t cap, Start* out) {
  uint32_t nh = 0;
  auto r = channel_read(boot, buf, cap, out->handles, abi::process::kMaxStartHandlesV2, kForever);
  if (!r.ok()) return r.status;
  uint32_t bytes = static_cast<uint32_t>(r.value >> 32);
  nh = static_cast<uint32_t>(r.value);
  uint32_t header_bytes;
  if (!abi::process::unpack_start_header(buf, bytes, &out->header, &header_bytes)) return Status::InvalidArgs;
  const auto& h = out->header;
  if (h.handle_count != nh) {
    return Status::InvalidArgs;
  }
  out->args = buf + header_bytes;
  out->env = out->args + h.args_bytes;
  return Status::Ok;
}

// i-я строка блока (строки с \0 подряд).
inline const char* nth_string(const char* block, uint32_t i) {
  while (i-- != 0) block += length(block) + 1;
  return block;
}

}  // namespace charm
