#include <lib/elf/elf64.hpp>

namespace lib::elf {

namespace {

constexpr uint64_t kPage = 4096;

constexpr uint64_t align_down(uint64_t v) { return v & ~(kPage - 1); }

// Сложение без переполнения: false, если результат не помещается в 64 бита.
constexpr bool add_ok(uint64_t a, uint64_t b, uint64_t* out) {
  *out = a + b;
  return *out >= a;
}

}  // namespace

const char* error_name(Error e) {
  switch (e) {
    case Error::None: return "none";
    case Error::TooSmall: return "file too small";
    case Error::BadMagic: return "bad ELF magic";
    case Error::NotClass64: return "not ELFCLASS64";
    case Error::NotLittleEndian: return "not ELFDATA2LSB";
    case Error::BadVersion: return "bad ELF version";
    case Error::NotExecutable: return "not ET_EXEC";
    case Error::WrongMachine: return "not EM_X86_64";
    case Error::BadPhdrTable: return "bad program header table";
    case Error::SegmentOutOfFile: return "segment outside file";
    case Error::SegmentFileGtMem: return "segment p_filesz > p_memsz";
    case Error::SegmentOverflow: return "segment address overflow";
    case Error::SegmentBelowBase: return "segment below kernel base";
    case Error::SegmentWriteExec: return "segment is both W and X";
    case Error::SegmentMisaligned: return "segment vaddr/offset misaligned";
    case Error::SegmentsOverlap: return "segments overlap";
    case Error::NoLoadSegments: return "no PT_LOAD segments";
    case Error::EntryOutsideSegments: return "entry point not in executable segment";
  }
  return "unknown";
}

Error parse_kernel_image(const void* data, size_t size, uint64_t min_vaddr, Image* out) {
  if (size < sizeof(Elf64Ehdr)) return Error::TooSmall;
  const auto* bytes = static_cast<const uint8_t*>(data);
  const auto* eh = static_cast<const Elf64Ehdr*>(data);

  if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
      eh->e_ident[3] != 'F') {
    return Error::BadMagic;
  }
  if (eh->e_ident[4] != kElfClass64) return Error::NotClass64;
  if (eh->e_ident[5] != kElfData2Lsb) return Error::NotLittleEndian;
  if (eh->e_ident[6] != kEvCurrent || eh->e_version != kEvCurrent) return Error::BadVersion;
  if (eh->e_type != kEtExec) return Error::NotExecutable;
  if (eh->e_machine != kEmX86_64) return Error::WrongMachine;

  if (eh->e_phentsize != sizeof(Elf64Phdr) || eh->e_phnum == 0 || eh->e_phnum == kPnXnum ||
      eh->e_phoff % 8 != 0) {
    return Error::BadPhdrTable;
  }
  uint64_t ph_end;
  if (!add_ok(eh->e_phoff, uint64_t{eh->e_phnum} * sizeof(Elf64Phdr), &ph_end) || ph_end > size) {
    return Error::BadPhdrTable;
  }
  const auto* ph = reinterpret_cast<const Elf64Phdr*>(bytes + eh->e_phoff);

  uint64_t lo = ~uint64_t{0};
  uint64_t hi = 0;
  bool any = false;
  bool entry_ok = false;
  for (uint16_t i = 0; i < eh->e_phnum; ++i) {
    const Elf64Phdr& p = ph[i];
    if (p.p_type != kPtLoad) continue;
    uint64_t file_end;
    if (!add_ok(p.p_offset, p.p_filesz, &file_end) || file_end > size) {
      return Error::SegmentOutOfFile;
    }
    if (p.p_filesz > p.p_memsz) return Error::SegmentFileGtMem;
    uint64_t mem_end;
    if (!add_ok(p.p_vaddr, p.p_memsz, &mem_end)) return Error::SegmentOverflow;
    if (p.p_vaddr < min_vaddr) return Error::SegmentBelowBase;
    if ((p.p_flags & kPfW) != 0 && (p.p_flags & kPfX) != 0) return Error::SegmentWriteExec;
    if (p.p_vaddr % kPage != p.p_offset % kPage) return Error::SegmentMisaligned;
    if (p.p_memsz == 0) continue;

    // Сегменты не должны делить страницы: у каждой страницы одни права.
    uint64_t a0 = align_down(p.p_vaddr);
    uint64_t a1 = align_down(mem_end - 1) + kPage;
    for (uint16_t j = 0; j < i; ++j) {
      const Elf64Phdr& q = ph[j];
      if (q.p_type != kPtLoad || q.p_memsz == 0) continue;
      uint64_t b0 = align_down(q.p_vaddr);
      uint64_t b1 = align_down(q.p_vaddr + q.p_memsz - 1) + kPage;
      if (a0 < b1 && b0 < a1) return Error::SegmentsOverlap;
    }

    if ((p.p_flags & kPfX) != 0 && eh->e_entry >= p.p_vaddr && eh->e_entry < mem_end) {
      entry_ok = true;
    }
    if (a0 < lo) lo = a0;
    if (a1 > hi) hi = a1;
    any = true;
  }
  if (!any) return Error::NoLoadSegments;
  if (!entry_ok) return Error::EntryOutsideSegments;

  out->ehdr = eh;
  out->phdrs = ph;
  out->phnum = eh->e_phnum;
  out->entry = eh->e_entry;
  out->vaddr_min = lo;
  out->vaddr_max = hi;
  return Error::None;
}

}  // namespace lib::elf
