#pragma once
// 4-уровневые таблицы страниц x86-64 (docs/specs/intel-sdm-vol3a.pdf §4.5,
// таблицы 4-15..4-20). Загрузчик строит их, пока Boot Services открыты, и
// пишет в них по физическому адресу (UEFI держит тождественное отображение).

#include <stddef.h>
#include <stdint.h>

namespace boot {

inline constexpr uint64_t kPageSize = 4096;
inline constexpr uint64_t kLargePageSize = 2ull << 20;

// Права листа. Промежуточные записи всегда P|RW, ограничения — в листе.
inline constexpr uint64_t kMapWrite = 1ull << 1;
inline constexpr uint64_t kMapNoExec = 1ull << 63;

// Выделяет одну обнулённую страницу 4 КиБ; возвращает её физический адрес
// или 0 при нехватке памяти.
using PageAllocator = uint64_t (*)(void* ctx);

class PageTables {
 public:
  bool init(PageAllocator alloc, void* ctx);
  uint64_t root() const { return pml4_; }

  // false — нет памяти, адреса не выровнены или страница уже отображена.
  bool map_4k(uint64_t virt, uint64_t phys, uint64_t flags);
  bool map_2m(uint64_t virt, uint64_t phys, uint64_t flags);
  // Отображает [virt, virt+size) на [phys, ...): страницы 2 МиБ там, где
  // virt и phys оба выровнены, иначе 4 КиБ. size кратен 4 КиБ.
  bool map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);

  // Для тестов: физ. адрес и флаги листа, либо false, если не отображено.
  bool translate(uint64_t virt, uint64_t* phys, uint64_t* flags) const;

 private:
  uint64_t* next_table(uint64_t* table, size_t index);

  PageAllocator alloc_ = nullptr;
  void* ctx_ = nullptr;
  uint64_t pml4_ = 0;
};

}  // namespace boot
