#include <lib/elf/elf64.hpp>

#include "test.hpp"

using namespace lib::elf;

namespace {

constexpr uint64_t kBase = 0xffffffff80000000ull;

// Образ: заголовок + 3 PT_LOAD (text RX, rodata R, data RW с .bss).
struct Fixture {
  alignas(8) uint8_t buf[3 * 4096];
  Elf64Ehdr* eh() { return reinterpret_cast<Elf64Ehdr*>(buf); }
  Elf64Phdr* ph() { return reinterpret_cast<Elf64Phdr*>(buf + sizeof(Elf64Ehdr)); }

  Fixture() {
    std::memset(buf, 0, sizeof buf);
    Elf64Ehdr& e = *eh();
    const uint8_t ident[] = {0x7f, 'E', 'L', 'F', kElfClass64, kElfData2Lsb, kEvCurrent};
    std::memcpy(e.e_ident, ident, sizeof ident);
    e.e_type = kEtExec;
    e.e_machine = kEmX86_64;
    e.e_version = kEvCurrent;
    e.e_entry = kBase + 0x10;
    e.e_phoff = sizeof(Elf64Ehdr);
    e.e_ehsize = sizeof(Elf64Ehdr);
    e.e_phentsize = sizeof(Elf64Phdr);
    e.e_phnum = 3;
    ph()[0] = {kPtLoad, kPfR | kPfX, 0, kBase, kBase, 4096, 4096, 4096};
    ph()[1] = {kPtLoad, kPfR, 4096, kBase + 0x1000, kBase + 0x1000, 4096, 4096, 4096};
    ph()[2] = {kPtLoad, kPfR | kPfW, 8192, kBase + 0x2000, kBase + 0x2000, 4096, 0x5000, 4096};
  }

  Error parse(Image* img = nullptr) {
    Image tmp;
    return parse_kernel_image(buf, sizeof buf, kBase, img ? img : &tmp);
  }
};

}  // namespace

TEST(elf_valid_image) {
  Fixture f;
  Image img;
  CHECK(f.parse(&img) == Error::None);
  CHECK(img.entry == kBase + 0x10);
  CHECK(img.vaddr_min == kBase);
  CHECK(img.vaddr_max == kBase + 0x7000);
  CHECK(img.phnum == 3);
}

TEST(elf_too_small) {
  Fixture f;
  Image img;
  CHECK(parse_kernel_image(f.buf, 10, kBase, &img) == Error::TooSmall);
}

TEST(elf_header_checks) {
  { Fixture f; f.eh()->e_ident[1] = 'X'; CHECK(f.parse() == Error::BadMagic); }
  { Fixture f; f.eh()->e_ident[4] = 1; CHECK(f.parse() == Error::NotClass64); }
  { Fixture f; f.eh()->e_ident[5] = 2; CHECK(f.parse() == Error::NotLittleEndian); }
  { Fixture f; f.eh()->e_version = 0; CHECK(f.parse() == Error::BadVersion); }
  { Fixture f; f.eh()->e_type = 3; CHECK(f.parse() == Error::NotExecutable); }
  { Fixture f; f.eh()->e_machine = 3; CHECK(f.parse() == Error::WrongMachine); }
}

TEST(elf_phdr_table_checks) {
  { Fixture f; f.eh()->e_phentsize = 32; CHECK(f.parse() == Error::BadPhdrTable); }
  { Fixture f; f.eh()->e_phnum = 0; CHECK(f.parse() == Error::BadPhdrTable); }
  { Fixture f; f.eh()->e_phnum = kPnXnum; CHECK(f.parse() == Error::BadPhdrTable); }
  { Fixture f; f.eh()->e_phoff = sizeof f.buf - 8; CHECK(f.parse() == Error::BadPhdrTable); }
  { Fixture f; f.eh()->e_phoff = ~uint64_t{0} - 8; CHECK(f.parse() == Error::BadPhdrTable); }
}

TEST(elf_segment_checks) {
  { Fixture f; f.ph()[2].p_filesz = 8192; CHECK(f.parse() == Error::SegmentOutOfFile); }
  { Fixture f; f.ph()[2].p_offset = ~uint64_t{0}; CHECK(f.parse() == Error::SegmentOutOfFile); }
  { Fixture f; f.ph()[1].p_memsz = 100; CHECK(f.parse() == Error::SegmentFileGtMem); }
  { Fixture f; f.ph()[2].p_memsz = ~uint64_t{0}; CHECK(f.parse() == Error::SegmentOverflow); }
  { Fixture f; f.ph()[1].p_vaddr = 0x1000; CHECK(f.parse() == Error::SegmentBelowBase); }
  { Fixture f; f.ph()[0].p_flags |= kPfW; CHECK(f.parse() == Error::SegmentWriteExec); }
  { Fixture f; f.ph()[1].p_vaddr += 8; CHECK(f.parse() == Error::SegmentMisaligned); }
  { Fixture f; f.ph()[1].p_vaddr = kBase + 0x2000; f.ph()[1].p_offset = 8192;
    f.ph()[1].p_filesz = f.ph()[1].p_memsz = 16; CHECK(f.parse() == Error::SegmentsOverlap); }
}

TEST(elf_entry_must_be_executable) {
  { Fixture f; f.eh()->e_entry = kBase + 0x1010; CHECK(f.parse() == Error::EntryOutsideSegments); }
  { Fixture f; f.eh()->e_entry = kBase + 0x9000; CHECK(f.parse() == Error::EntryOutsideSegments); }
}

TEST(elf_no_load_segments) {
  Fixture f;
  for (int i = 0; i < 3; ++i) f.ph()[i].p_type = 4;  // PT_NOTE
  CHECK(f.parse() == Error::NoLoadSegments);
}

TEST(elf_error_names_defined) {
  for (uint32_t i = 0; i <= static_cast<uint32_t>(Error::EntryOutsideSegments); ++i) {
    CHECK(std::strcmp(error_name(static_cast<Error>(i)), "unknown") != 0);
  }
}
