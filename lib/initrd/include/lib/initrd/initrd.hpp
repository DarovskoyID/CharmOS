#pragma once
// Чтение initrd (docs/abi/initrd.md). Не копирует данные: файлы — это
// указатели внутрь архива.

#include <stddef.h>
#include <stdint.h>

#include <abi/initrd.hpp>
#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

namespace lib::initrd {

enum class InitrdError : uint8_t {
  TooSmall,
  BadMagic,
  BadVersion,
  BadSize,
  BadEntry,
  DuplicateName,
};

const char* error_name(InitrdError e);

struct File {
  lib::core::StringView name;
  const uint8_t* data;
  uint64_t size;
};

class Archive {
 public:
  // Проверяет весь архив по правилам docs/abi/initrd.md.
  lib::core::Status<InitrdError> open(const void* data, uint64_t size);

  uint32_t count() const { return count_; }
  File file(uint32_t index) const;
  // Поиск по имени; false — нет такого файла.
  bool find(lib::core::StringView name, File* out) const;

 private:
  const uint8_t* base_ = nullptr;
  const abi::initrd::Entry* entries_ = nullptr;
  uint32_t count_ = 0;
};

}  // namespace lib::initrd
