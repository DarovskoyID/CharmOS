#include "process.hpp"

#include <abi/syscall.hpp>
#include <arch/x86_64/cpu.hpp>
#include <lib/elf/elf64.hpp>

#include "../core/panic.hpp"
#include "../core/placement_new.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"

extern "C" [[noreturn]] void enter_user(uint64_t rip, uint64_t rsp);

namespace kernel::proc {

using arch::x86_64::kMapNoExec;
using arch::x86_64::kMapWrite;
using arch::x86_64::kPageSize;
using lib::core::Result;
using lib::core::StringView;

namespace {

constinit uint64_t g_next_pid = 1;

// FXSAVE-образ начального состояния (SDM Vol. 2A, FXSAVE, табл. 3-43):
// FCW (смещение 0) = 0x037F, MXCSR (смещение 24) = 0x1F80 — значения после
// FNINIT и по умолчанию psABI §3.2.3. Остальное нулевое (теги x87 — пусто).
void init_fpu_image(uint8_t* image) {
  for (int i = 0; i < 512; ++i) image[i] = 0;
  image[0] = 0x7f;
  image[1] = 0x03;
  uint32_t mxcsr = 0x1f80;
  __builtin_memcpy(image + 24, &mxcsr, sizeof mxcsr);
}

bool load_segments(Process* p, const lib::elf::Image& img, const uint8_t* file) {
  for (uint16_t i = 0; i < img.phnum; ++i) {
    const lib::elf::Elf64Phdr& ph = img.phdrs[i];
    if (ph.p_type != lib::elf::kPtLoad || ph.p_memsz == 0) continue;
    uint64_t flags = 0;
    if ((ph.p_flags & lib::elf::kPfW) != 0) flags |= kMapWrite;
    if ((ph.p_flags & lib::elf::kPfX) == 0) flags |= kMapNoExec;
    uint64_t v0 = ph.p_vaddr & ~(kPageSize - 1);
    uint64_t v1 = (ph.p_vaddr + ph.p_memsz + kPageSize - 1) & ~(kPageSize - 1);
    for (uint64_t v = v0; v < v1; v += kPageSize) {
      uint64_t phys = 0;
      if (p->space.map_new(v, flags, &phys).is_err()) return false;
      // Байты файла, попадающие на эту страницу; остальное уже обнулено.
      uint64_t seg_file_end = ph.p_vaddr + ph.p_filesz;
      uint64_t from = v < ph.p_vaddr ? ph.p_vaddr : v;
      uint64_t to = v + kPageSize < seg_file_end ? v + kPageSize : seg_file_end;
      auto* dst = mm::phys_to_virt<uint8_t>(phys);
      for (uint64_t a = from; a < to; ++a) dst[a - v] = file[ph.p_offset + (a - ph.p_vaddr)];
    }
  }
  return true;
}

void process_main(void* arg) {
  auto* p = static_cast<Process*>(arg);
  enter_user(p->entry, kUserStackTop);
}

void free_process(Process* p) {
  if (p->fpu != nullptr) mm::kfree(p->fpu);
  mm::kfree(p);
}

}  // namespace

Result<Process*, ProcError> spawn_process(StringView name, const void* elf, size_t size, int cpu) {
  using R = Result<Process*, ProcError>;
  lib::elf::Image img;
  auto err = lib::elf::parse_image(elf, size, kUserImageMin, kUserStackTop - kUserStackSize, &img);
  if (err != lib::elf::Error::None) {
    kprint("proc: {}: bad ELF: {}\n", name, lib::elf::error_name(err));
    return R::err(ProcError::BadImage);
  }

  auto mem = mm::kmalloc(sizeof(Process));
  if (mem.is_err()) return R::err(ProcError::OutOfMemory);
  auto* p = static_cast<Process*>(mem.value());
  // Process содержит Semaphore с конструктором — заполняем поля явно.
  __builtin_memset(static_cast<void*>(p), 0, sizeof(Process));
  new (&p->exited) sync::Semaphore("proc.exited", 0);
  p->pid = __atomic_fetch_add(&g_next_pid, 1, __ATOMIC_RELAXED);
  size_t n = name.size() < sizeof p->name - 1 ? name.size() : sizeof p->name - 1;
  for (size_t i = 0; i < n; ++i) p->name[i] = name[i];
  p->entry = img.entry;

  auto fpu = mm::kmalloc(512);
  if (fpu.is_err() || p->space.create().is_err()) {
    if (fpu.is_ok()) mm::kfree(fpu.value());
    mm::kfree(p);
    return R::err(ProcError::OutOfMemory);
  }
  p->fpu = static_cast<uint8_t*>(fpu.value());
  init_fpu_image(p->fpu);

  bool ok = load_segments(p, img, static_cast<const uint8_t*>(elf));
  for (uint64_t v = kUserStackTop - kUserStackSize; ok && v < kUserStackTop; v += kPageSize) {
    ok = p->space.map_new(v, kMapWrite | kMapNoExec, nullptr).is_ok();
  }
  sched::UserContext user{p->space.root(), p, p->fpu};
  if (!ok || sched::spawn(p->name, &process_main, p, cpu, &user).is_err()) {
    p->space.destroy();
    free_process(p);
    return R::err(ProcError::OutOfMemory);
  }
  return R::ok(p);
}

int64_t wait_process(Process* p) {
  p->exited.down();
  int64_t code = p->exit_code;
  free_process(p);
  return code;
}

Process* current_process() { return static_cast<Process*>(sched::current()->process); }

void exit_current(int64_t code) {
  Process* p = current_process();
  if (p == nullptr) panic("proc: exit from a kernel thread");
  // Сначала уйти с CR3 процесса, потом разбирать его таблицы.
  arch::x86_64::cli();
  arch::x86_64::write_cr3(mm::kernel_root());
  sched::detach_user_context();
  p->space.destroy();
  p->exit_code = code;
  p->exited.up();  // после этого p может быть освобождён ожидающим
  sched::exit_thread();
}

void kill_current(const arch::x86_64::InterruptFrame& f) {
  Process* p = current_process();
  if (p == nullptr) panic("proc: user-mode exception without a process");
  if (f.vector == arch::x86_64::kVectorPageFault) {
    kprint("proc: {} (pid {}) killed: {} at rip={X} addr={X} error={x}\n", p->name, p->pid,
           arch::x86_64::exception_name(f.vector), f.rip, arch::x86_64::read_cr2(), f.error_code);
  } else {
    kprint("proc: {} (pid {}) killed: {} at rip={X} error={x}\n", p->name, p->pid,
           arch::x86_64::exception_name(f.vector), f.rip, f.error_code);
  }
  exit_current(kExitKilled);
}

}  // namespace kernel::proc
