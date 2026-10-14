#pragma once
// 4-уровневые таблицы страниц x86-64 (docs/specs/intel-sdm-vol3a.pdf §4.5,
// таблицы 4-15..4-20). Общий код загрузчика и ядра: загрузчик обращается к
// таблицам по физическому адресу (phys_offset = 0, у UEFI тождественное
// отображение), ядро — через HHDM (phys_offset = hhdm_base).

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64 {

inline constexpr uint64_t kPageSize = 4096;
inline constexpr uint64_t kLargePageSize = 2ull << 20;

// Права и тип кэширования листа. Промежуточные записи всегда P|RW,
// ограничения задаются только в листе.
inline constexpr uint64_t kMapWrite = 1ull << 1;
// U/S: доступно из ring 3. SDM §4.6: адрес пользовательский, только если U/S=1
// во всех записях пути, поэтому флаг ставится и в промежуточные записи.
inline constexpr uint64_t kMapUser = 1ull << 2;
inline constexpr uint64_t kMapCacheDisable = 1ull << 4;  // PCD: для MMIO
inline constexpr uint64_t kMapNoExec = 1ull << 63;

// Выделяет одну страницу 4 КиБ (обнулять не обязан); возвращает её
// физический адрес или 0 при нехватке памяти.
using PageAllocator = uint64_t (*)(void* ctx);

class PageTables {
 public:
  // Создаёт пустую PML4.
  bool init(PageAllocator alloc, void* ctx, uint64_t phys_offset);
  // Оборачивает существующие таблицы (только для translate).
  void attach(uint64_t root, uint64_t phys_offset);
  uint64_t root() const { return pml4_; }

  // false — нет памяти, адреса не выровнены или страница уже отображена.
  bool map_4k(uint64_t virt, uint64_t phys, uint64_t flags);
  bool map_2m(uint64_t virt, uint64_t phys, uint64_t flags);
  // Отображает [virt, virt+size) на [phys, ...): страницы 2 МиБ там, где
  // virt и phys оба выровнены, иначе 4 КиБ. size кратен 4 КиБ.
  bool map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);

  // Снимает отображение страницы 4 КиБ (таблицы не освобождаются). Возвращает
  // физ. адрес снятой страницы или 0, если её не было. TLB сбрасывает вызывающий.
  uint64_t unmap_4k(uint64_t virt);

  // Физ. адрес и флаги листа, либо false, если не отображено.
  bool translate(uint64_t virt, uint64_t* phys, uint64_t* flags) const;

  // Создаёт таблицу следующего уровня в PML4[index], если её нет. Ядро так
  // заранее заполняет всю верхнюю половину: записи PML4 256..511 больше не
  // меняются, и адресные пространства процессов копируют их один раз.
  bool ensure_pml4_entry(size_t index);

  // Освобождает нижнюю половину (PML4 0..255): каждый лист 4 КиБ — через
  // free_page (это страницы процесса), каждую таблицу — тоже. Большие
  // страницы в нижней половине не поддерживаются (процессы их не получают).
  using PageFree = void (*)(void* ctx, uint64_t phys);
  void destroy_lower_half(PageFree free_page, void* ctx);

 private:
  uint64_t* table(uint64_t phys) const;
  uint64_t* next_table(uint64_t* table, size_t index, bool user);

  PageAllocator alloc_ = nullptr;
  void* ctx_ = nullptr;
  uint64_t pml4_ = 0;
  uint64_t offset_ = 0;
};

}  // namespace arch::x86_64
