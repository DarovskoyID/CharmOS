#include <lib/initrd/initrd.hpp>

#include "test.hpp"

using abi::initrd::Entry;
using abi::initrd::Header;
using lib::core::StringView;
using lib::initrd::Archive;
using lib::initrd::InitrdError;

namespace {

// Архив из двух файлов. Заголовок 32 Б + 2 записи по 64 Б = таблица до 160;
// "a" (5 байт) на 160, "bin/b" (3 байта) на 176, всего 192 байта.
struct Image {
  alignas(16) unsigned char buf[256] = {};
  Header* h() { return reinterpret_cast<Header*>(buf); }
  Entry* e() { return reinterpret_cast<Entry*>(buf + sizeof(Header)); }
  Image() {
    std::memcpy(h()->magic, abi::initrd::kMagic, 8);
    h()->version = abi::initrd::kVersion;
    h()->entry_count = 2;
    h()->total_size = 192;
    std::memcpy(e()[0].name, "a", 1);
    e()[0].offset = 160;
    e()[0].size = 5;
    std::memcpy(e()[1].name, "bin/b", 5);
    e()[1].offset = 176;
    e()[1].size = 3;
    std::memcpy(buf + 160, "hello", 5);
    std::memcpy(buf + 176, "bin", 3);
  }
  lib::core::Status<InitrdError> open(Archive* a, uint64_t size = 192) { return a->open(buf, size); }
};

}  // namespace

TEST(initrd_valid_archive) {
  Image img;
  Archive a;
  CHECK(img.open(&a).is_ok());
  CHECK(a.count() == 2);
  lib::initrd::File f;
  CHECK(a.find("bin/b", &f));
  CHECK(f.size == 3 && std::memcmp(f.data, "bin", 3) == 0);
  CHECK(a.find("a", &f) && f.size == 5 && std::memcmp(f.data, "hello", 5) == 0);
  CHECK(!a.find("missing", &f));
  CHECK(a.file(1).name == StringView("bin/b"));
}

TEST(initrd_rejects_header_problems) {
  { Image img; Archive a; CHECK(a.open(img.buf, 8).error() == InitrdError::TooSmall); }
  { Image img; img.buf[0] = 'X'; Archive a; CHECK(img.open(&a).error() == InitrdError::BadMagic); }
  { Image img; img.h()->version = 2; Archive a; CHECK(img.open(&a).error() == InitrdError::BadVersion); }
  { Image img; Archive a; CHECK(img.open(&a, 100).error() == InitrdError::BadSize); }
  { Image img; img.h()->entry_count = 1000; Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
}

TEST(initrd_rejects_bad_entries) {
  { Image img; img.e()[0].offset = 161; Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
  { Image img; img.e()[0].offset = 32; Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
  { Image img; img.e()[1].size = 1000; Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
  { Image img; img.e()[1].size = ~uint64_t{0}; Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
  { Image img; img.e()[0].name[0] = 0; Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
  { Image img; std::memset(img.e()[0].name, 'x', 48); Archive a; CHECK(img.open(&a).error() == InitrdError::BadEntry); }
  { Image img; std::memcpy(img.e()[1].name, "a\0\0\0\0", 5); Archive a; CHECK(img.open(&a).error() == InitrdError::DuplicateName); }
}
