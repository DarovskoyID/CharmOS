// Закрытая область (docs/abi/sysarea-crypt.md): заголовок, первичное
// шифрование на месте (в том числе прерванное) и ext2 поверх XtsIo.
#include <lib/fs/ext2.hpp>
#include <lib/sysarea/sysarea.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "test.hpp"

using namespace lib::sysarea;
using lib::core::StringView;

namespace {

class MemIo final : public lib::fs::Io {
 public:
  explicit MemIo(size_t n) : d(n, 0) {}
  lib::fs::Status read(uint64_t o, void* b, size_t l) override {
    if (o + l > d.size()) return lib::fs::fail(lib::fs::FsError::IoError);
    std::memcpy(b, d.data() + o, l);
    return lib::fs::ok();
  }
  lib::fs::Status write(uint64_t o, const void* b, size_t l) override {
    if (o + l > d.size()) return lib::fs::fail(lib::fs::FsError::IoError);
    std::memcpy(d.data() + o, b, l);
    return lib::fs::ok();
  }
  lib::fs::Status flush() override { return lib::fs::ok(); }
  uint64_t size() const override { return d.size(); }
  std::vector<uint8_t> d;
};

constexpr size_t kPart = 8 * 1024 * 1024;
const char kSecret[] = "store package: top secret bytes";

lib::crypto::Xts256 test_key() {
  uint8_t k[kKeySize];
  for (size_t i = 0; i < kKeySize; ++i) k[i] = static_cast<uint8_t>(i * 7 + 1);
  lib::crypto::Xts256 x;
  (void)x.set_key(k);
  return x;
}

// Открытая область, как её кладёт mkdisk: заголовок и ext2 с одним файлом.
void make_plain_area(MemIo& part, uint64_t* units) {
  Header h;
  make_plain(&h, part.size());
  std::memcpy(part.d.data(), &h, sizeof h);
  *units = h.f.data_units;
  lib::crypto::Xts256 none;
  XtsIo io;
  io.init(&part, &none, *units, 0);
  lib::fs::Ext2 fs;
  CHECK(lib::fs::Ext2::format(io, 1024, "sys").is_ok());
  CHECK(fs.mount(&io).is_ok());
  auto f = fs.create(fs.root(), StringView("secret"), lib::fs::NodeType::File);
  CHECK(f.is_ok());
  CHECK(fs.write(f.value(), 0, kSecret, sizeof kSecret).is_ok());
  CHECK(fs.sync().is_ok());
}

bool contains(const std::vector<uint8_t>& d, const char* s) {
  std::string all(d.begin(), d.end());
  return all.find(s) != std::string::npos;
}

void check_reads(MemIo& part, const lib::crypto::Xts256& key, uint64_t units, uint64_t limit) {
  XtsIo io;
  io.init(&part, &key, units, limit);
  lib::fs::Ext2 fs;
  CHECK(fs.mount(&io).is_ok());
  auto f = fs.lookup(fs.root(), StringView("secret"));
  CHECK(f.is_ok());
  char buf[sizeof kSecret] = {};
  auto n = fs.read(f.value(), 0, buf, sizeof buf);
  CHECK(n.is_ok() && std::memcmp(buf, kSecret, sizeof kSecret) == 0);
}

}  // namespace

TEST(sysarea_header) {
  Header h;
  make_plain(&h, kPart);
  CHECK(valid(h, kPart));
  CHECK(h.f.data_units == (kPart - kDataOffset) / kUnit);
  CHECK(!valid(h, kDataOffset + 512));  // раздел меньше, чем говорит заголовок
  h.f.state = static_cast<uint32_t>(State::Encrypted);
  CHECK(!valid(h, kPart));  // поле изменено без seal_check
  seal_check(&h);
  CHECK(valid(h, kPart));
}

TEST(sysarea_encrypt_in_place) {
  MemIo part(kPart);
  uint64_t units = 0;
  make_plain_area(part, &units);
  CHECK(contains(part.d, kSecret));
  auto key = test_key();
  CHECK(encrypt_units(&part, key, 0, units).is_ok());
  CHECK(!contains(part.d, kSecret));  // на носителе — только шифротекст
  check_reads(part, key, units, units);
}

TEST(sysarea_interrupted_encryption) {
  // Сбой питания посередине: до progress — зашифровано, после — открыто.
  MemIo part(kPart);
  uint64_t units = 0;
  make_plain_area(part, &units);
  auto key = test_key();
  uint64_t half = units / 2;
  CHECK(encrypt_units(&part, key, 0, half).is_ok());
  check_reads(part, key, units, half);
  CHECK(encrypt_units(&part, key, half, units).is_ok());
  check_reads(part, key, units, units);
}
