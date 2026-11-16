#pragma once
// Адресное пространство процесса: своя PML4, нижняя половина — память
// процесса, верхняя — общая с ядром (записи PML4 256..511 скопированы из
// таблиц ядра; ядро заполнило их заранее, поэтому они не меняются).

#include <arch/x86_64/paging.hpp>
#include <abi/syscall.hpp>
#include <lib/core/result.hpp>

#include "vmm.hpp"

namespace kernel::mm {

class AddressSpace {
 public:
  struct FrameRun { uint64_t phys, pages; };
  lib::core::Status<VmError> create();

  // Отображает свежий обнулённый фрейм по адресу virt (нижняя половина) с
  // правами пользователя; phys_out — его физический адрес (для заполнения
  // через HHDM).
  lib::core::Status<VmError> map_new(uint64_t virt, uint64_t flags, uint64_t* phys_out);
  // Eager loader batches private frames, falling back to one page if physical
  // fragmentation prevents a run. No backing is shared with the filesystem.
  // zero=false leaves the frames' old contents in place; the caller must fill
  // every byte it exposes to user space before the process can run.
  lib::core::Result<FrameRun, VmError> map_new_run(uint64_t virt, uint64_t pages, uint64_t flags,
                                                   bool zero = true);

  // Отображает существующий фрейм (страница VMO) с правами пользователя.
  lib::core::Status<VmError> map_phys(uint64_t virt, uint64_t phys, uint64_t flags);
  bool prepare_map(uint64_t virt) {
    return virt < abi::syscall::kUserTop &&
        pt_.prepare_4k(virt, arch::x86_64::kMapUser);
  }
  // Снимает отображение, не освобождая фрейм. TLB сбрасывает вызывающий.
  void unmap(uint64_t virt);
  bool protect(uint64_t virt, uint64_t flags);
  // Меняет и физ. страницу, и права существующего листа (present или
  // inaccessible-маркер). Нужно для ленивого VMO: другой alias мог выделить
  // фрейм, пока здесь стоял маркер с физ. адресом 0. TLB сбрасывает вызывающий.
  bool protect_phys(uint64_t virt, uint64_t phys, uint64_t flags);

  uint64_t root() const { return pt_.root(); }

  // Освобождает все страницы процесса, таблицы и PML4. CR3 текущего CPU не
  // должен указывать на это пространство.
  void destroy();

 private:
  arch::x86_64::PageTables pt_;
};

}  // namespace kernel::mm
