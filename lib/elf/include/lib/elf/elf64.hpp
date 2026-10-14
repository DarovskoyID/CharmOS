#pragma once
// ELF64 по docs/specs/elf64.pdf (ELF-64 Object File Format 1.5),
// x86-64-специфика — docs/specs/x86-64-psabi.pdf §4.1.

#include <stddef.h>
#include <stdint.h>

namespace lib::elf {

struct Elf64Ehdr {
  uint8_t e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};
static_assert(sizeof(Elf64Ehdr) == 64);
static_assert(offsetof(Elf64Ehdr, e_entry) == 24);
static_assert(offsetof(Elf64Ehdr, e_phoff) == 32);
static_assert(offsetof(Elf64Ehdr, e_phentsize) == 54);
static_assert(offsetof(Elf64Ehdr, e_phnum) == 56);

struct Elf64Phdr {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
};
static_assert(sizeof(Elf64Phdr) == 56);
static_assert(offsetof(Elf64Phdr, p_vaddr) == 16);
static_assert(offsetof(Elf64Phdr, p_memsz) == 40);

inline constexpr uint8_t kElfClass64 = 2;
inline constexpr uint8_t kElfData2Lsb = 1;
inline constexpr uint8_t kEvCurrent = 1;
inline constexpr uint16_t kEtExec = 2;
inline constexpr uint16_t kEmX86_64 = 62;
inline constexpr uint32_t kPtLoad = 1;
inline constexpr uint16_t kPnXnum = 0xffff;
inline constexpr uint32_t kPfX = 0x1;
inline constexpr uint32_t kPfW = 0x2;
inline constexpr uint32_t kPfR = 0x4;

enum class Error : uint32_t {
  None = 0,
  TooSmall,
  BadMagic,
  NotClass64,
  NotLittleEndian,
  BadVersion,
  NotExecutable,
  WrongMachine,
  BadPhdrTable,
  SegmentOutOfFile,     // p_offset + p_filesz за пределами файла
  SegmentFileGtMem,     // p_filesz > p_memsz
  SegmentOverflow,      // p_vaddr + p_memsz переполняет адресное пространство
  SegmentBelowBase,     // p_vaddr ниже допустимой базы
  SegmentWriteExec,     // нарушение W^X
  SegmentMisaligned,    // p_vaddr % 4096 != p_offset % 4096
  SegmentsOverlap,
  NoLoadSegments,
  EntryOutsideSegments, // e_entry не в исполняемом сегменте
};

const char* error_name(Error e);

// Результат проверки исполняемого файла ядра.
struct Image {
  const Elf64Ehdr* ehdr;
  const Elf64Phdr* phdrs;  // все заголовки программы (e_phnum штук)
  uint16_t phnum;
  uint64_t entry;
  uint64_t vaddr_min;      // нижний адрес PT_LOAD, округлён вниз до 4 КиБ
  uint64_t vaddr_max;      // верхний адрес PT_LOAD, округлён вверх до 4 КиБ
};

// Проверяет ELF64-образ ядра по контракту docs/abi/boot.md §3:
// ET_EXEC, EM_X86_64, все PT_LOAD внутри файла, не ниже min_vaddr,
// без W+X, без пересечений, точка входа в исполняемом сегменте.
// data должен быть выровнен на 8.
Error parse_kernel_image(const void* data, size_t size, uint64_t min_vaddr, Image* out);

}  // namespace lib::elf
