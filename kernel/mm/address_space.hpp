#pragma once
// Адресное пространство процесса: своя PML4, нижняя половина — память
// процесса, верхняя — общая с ядром (записи PML4 256..511 скопированы из
// таблиц ядра; ядро заполнило их заранее, поэтому они не меняются).

#include <arch/x86_64/paging.hpp>
#include <lib/core/result.hpp>

#include "vmm.hpp"

namespace kernel::mm {

class AddressSpace {
 public:
  lib::core::Status<VmError> create();

  // Отображает свежий обнулённый фрейм по адресу virt (нижняя половина) с
  // правами пользователя; phys_out — его физический адрес (для заполнения
  // через HHDM).
  lib::core::Status<VmError> map_new(uint64_t virt, uint64_t flags, uint64_t* phys_out);

  // Отображает существующий фрейм (страница VMO) с правами пользователя.
  lib::core::Status<VmError> map_phys(uint64_t virt, uint64_t phys, uint64_t flags);
  // Снимает отображение, не освобождая фрейм. TLB сбрасывает вызывающий.
  void unmap(uint64_t virt);

  uint64_t root() const { return pt_.root(); }

  // Освобождает все страницы процесса, таблицы и PML4. CR3 текущего CPU не
  // должен указывать на это пространство.
  void destroy();

 private:
  arch::x86_64::PageTables pt_;
};

}  // namespace kernel::mm
