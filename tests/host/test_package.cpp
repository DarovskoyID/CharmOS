// Пакет .cpk (docs/abi/package.md): сборка tools/sign, разбор, подпись, хеши.
#include <lib/crypto/ed25519.hpp>
#include <lib/package/package.hpp>

#include <cstring>

#include "../../tools/sign/pack.hpp"
#include "test.hpp"

namespace pk = abi::package;
using lib::package::Error;

namespace {

const uint8_t kSeed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
const uint8_t kOther[32] = {9};

const char kManifest[] =
    "# пример\n"
    "id = \"org.example.hello\"\n"
    "name = \"Hello\"\n"
    "version = \"1.0.0\"\n"
    "exec = \"bin/hello\"\n"
    "permissions = [\"net\", \"files\"]  # права\n";

std::vector<uint8_t> sample(const uint8_t* seed) {
  std::vector<sign::InFile> f = {{"bin/hello", std::vector<uint8_t>(1000, 0x7f), true},
                                 {"share/readme.txt", {'h', 'i', '\n'}, false}};
  return sign::pack(kManifest, f, seed);
}

}  // namespace

TEST(package_roundtrip) {
  auto p = sample(kSeed);
  auto v = lib::package::open(p.data(), p.size());
  CHECK(v.is_ok());
  if (v.is_err()) return;
  CHECK(std::strcmp(v.value().manifest.id, "org.example.hello") == 0);
  CHECK(v.value().manifest.permissions == (pk::kPermNet | pk::kPermFiles));
  uint8_t pub[32];
  lib::crypto::ed25519_public_key(kSeed, pub);
  CHECK(lib::package::check_signature(v.value(), pub) == Error::Ok);
  CHECK(lib::package::check_signature(v.value(), nullptr) == Error::Ok);
  CHECK(lib::package::check_files(v.value()) == Error::Ok);
  uint8_t other[32];
  lib::crypto::ed25519_public_key(kOther, other);
  CHECK(lib::package::check_signature(v.value(), other) == Error::UntrustedKey);
}

TEST(package_unsigned) {
  auto p = sample(nullptr);
  auto v = lib::package::open(p.data(), p.size());
  CHECK(v.is_ok() && lib::package::check_signature(v.value(), nullptr) == Error::Unsigned);
}

TEST(package_tampered) {
  uint8_t pub[32];
  lib::crypto::ed25519_public_key(kSeed, pub);
  // Изменён файл: подпись цела, хеш не сходится.
  auto p = sample(kSeed);
  auto v = lib::package::open(p.data(), p.size());
  const pk::FileEntry* e = lib::package::find(v.value(), "bin/hello");
  p[e->offset + 10] ^= 1;
  CHECK(lib::package::check_signature(v.value(), pub) == Error::Ok);
  CHECK(lib::package::check_files(v.value()) == Error::BadHash);
  // Изменён манифест: подпись не сходится.
  auto q = sample(kSeed);
  auto w = lib::package::open(q.data(), q.size());
  q[w.value().header->manifest_offset + 30] ^= 1;
  CHECK(lib::package::check_signature(w.value(), pub) == Error::BadSignature);
  // Испорчена подпись.
  auto r = sample(kSeed);
  auto x = lib::package::open(r.data(), r.size());
  r[x.value().header->signature_offset + 5] ^= 1;
  CHECK(lib::package::check_signature(x.value(), pub) == Error::BadSignature);
}

TEST(package_meta) {
  // package.sig: подписанная часть и блок подписи — проверка без данных.
  auto p = sample(kSeed);
  auto v = lib::package::open(p.data(), p.size());
  const auto& h = *v.value().header;
  std::vector<uint8_t> meta(p.begin(), p.begin() + static_cast<long>(h.data_offset));
  meta.insert(meta.end(), p.begin() + static_cast<long>(h.signature_offset), p.end());
  auto m = lib::package::open_meta(meta.data(), meta.size());
  uint8_t pub[32];
  lib::crypto::ed25519_public_key(kSeed, pub);
  CHECK(m.is_ok() && lib::package::check_signature(m.value(), pub) == Error::Ok);
  const pk::FileEntry* e = lib::package::find(m.value(), "bin/hello");
  CHECK(e != nullptr && lib::package::check_file(*e, p.data() + e->offset, e->size));
}

TEST(package_bad) {
  auto p = sample(kSeed);
  CHECK(lib::package::open(p.data(), 10).error() == Error::Truncated);
  auto q = p;
  q[0] = 'X';
  CHECK(lib::package::open(q.data(), q.size()).error() == Error::BadMagic);
  // «..» в пути.
  std::vector<sign::InFile> f = {{"../evil", {1}, true}};
  auto r = sign::pack("id = \"a\"\nname = \"a\"\nversion = \"1\"\nexec = \"../evil\"\n", f, nullptr);
  CHECK(lib::package::open(r.data(), r.size()).error() == Error::BadPath);
  // exec не исполняемый.
  std::vector<sign::InFile> g = {{"bin/a", {1}, false}};
  auto s = sign::pack("id = \"a\"\nname = \"a\"\nversion = \"1\"\nexec = \"bin/a\"\n", g, nullptr);
  CHECK(lib::package::open(s.data(), s.size()).error() == Error::NoExec);
}

TEST(manifest_errors) {
  using lib::package::parse_manifest;
  CHECK(parse_manifest("id = \"a\"\nname = \"a\"\nversion = \"1\"\n").is_err());                 // нет exec
  CHECK(parse_manifest("id = \"A\"\nname = \"a\"\nversion = \"1\"\nexec = \"x\"\n").is_err());   // id с заглавной
  CHECK(parse_manifest("id = \"a\"\nname = \"a\"\nversion = \"1\"\nexec = \"x\"\nfoo = \"1\"\n").is_err());
  CHECK(parse_manifest("id = \"a\"\nname = \"a\"\nversion = \"1\"\nexec = \"x\"\npermissions = [\"root\"]\n")
            .is_err());
  auto ok = parse_manifest(
      "id = \"a\"\nname = \"a\"\nversion = \"1\"\nexec = \"x\"\npermissions = [\"home\", \"console\", \"net-listen\"]\n");
  CHECK(ok.is_ok() && ok.value().permissions == (pk::kPermHome | pk::kPermConsole | pk::kPermNetListen));
}
