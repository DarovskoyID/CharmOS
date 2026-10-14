#pragma once
// Page cache: страницы дисков по 4 КиБ поверх блочного слоя. Запись
// отложенная: на диск — при вытеснении и при flush. Одна блокировка на весь
// кэш и поиск перебором (правило 9: устройство кэша — после замеров).

#include <lib/fs/fs.hpp>
#include <stdint.h>

namespace kernel::fs {

// Окно раздела диска disk: [offset, offset + size) в байтах.
class DiskIo final : public lib::fs::Io {
 public:
  constexpr DiskIo() = default;
  void init(uint32_t disk, uint64_t offset, uint64_t size) {
    disk_ = disk;
    offset_ = offset;
    size_ = size;
  }
  lib::fs::Status read(uint64_t offset, void* buf, size_t len) override;
  lib::fs::Status write(uint64_t offset, const void* buf, size_t len) override;
  // Грязные страницы диска — на устройство, затем сброс его кэша записи.
  lib::fs::Status flush() override;
  uint64_t size() const override { return size_; }

 private:
  uint32_t disk_ = 0;
  uint64_t offset_ = 0;
  uint64_t size_ = 0;
};

struct CacheStats {
  uint64_t hits;
  uint64_t misses;
  uint64_t writebacks;
};
CacheStats cache_stats();

}  // namespace kernel::fs
