#pragma once
// Сборка пакета .cpk (docs/abi/package.md) на хосте: для tools/sign и тестов.

#include <cstdint>
#include <string>
#include <vector>

namespace sign {

struct InFile {
  std::string path;  // путь внутри пакета
  std::vector<uint8_t> data;
  bool exec;
};

// seed == nullptr — пакет без подписи.
std::vector<uint8_t> pack(const std::string& manifest, const std::vector<InFile>& files, const uint8_t* seed);

}  // namespace sign
