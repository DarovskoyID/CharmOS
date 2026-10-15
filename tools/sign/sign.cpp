// tools/sign — ключи и пакеты .cpk (docs/abi/package.md §6).
//   sign keygen <имя>                    → <имя>.key (секрет, hex) и <имя>.pub (hex)
//   sign pack <manifest.toml> <ключ|-> <out.cpk> <путь=файл[:x]>...
//                                         x — исполняемый; «-» вместо ключа — без подписи
//   sign verify <пакет.cpk> [<ключ.pub>]  → подпись, хеши, манифест
//   sign pubkey-header <ключ.pub> <out.hpp>  → открытый ключ для сборки ОС
// Закрытый ключ магазина хранится только у автора, вне репозитория.
#include <lib/crypto/ed25519.hpp>
#include <lib/package/package.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "pack.hpp"

namespace {

[[noreturn]] void die(const std::string& msg) {
  std::fprintf(stderr, "sign: %s\n", msg.c_str());
  std::exit(1);
}

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) die("cannot read " + path);
  return {std::istreambuf_iterator<char>(f), {}};
}

void write_file(const std::string& path, const void* data, size_t len) {
  std::ofstream f(path, std::ios::binary);
  if (!f.write(static_cast<const char*>(data), static_cast<std::streamsize>(len))) die("cannot write " + path);
}

std::string to_hex(const uint8_t* p, size_t n) {
  std::string s;
  char b[3];
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(b, sizeof b, "%02x", p[i]);
    s += b;
  }
  return s;
}

std::vector<uint8_t> from_hex_file(const std::string& path, size_t want) {
  auto t = read_file(path);
  std::vector<uint8_t> v;
  auto nib = [](int c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i + 1 < t.size(); i += 2) {
    int a = nib(t[i]), b = nib(t[i + 1]);
    if (a < 0 || b < 0) break;
    v.push_back(static_cast<uint8_t>(a * 16 + b));
  }
  if (v.size() != want) die("bad key file " + path);
  return v;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) die("usage: sign keygen|pack|verify|pubkey-header ...");
  std::string cmd = argv[1];
  if (cmd == "keygen" && argc == 3) {
    uint8_t seed[32], pub[32];
    std::ifstream r("/dev/urandom", std::ios::binary);
    if (!r.read(reinterpret_cast<char*>(seed), sizeof seed)) die("no /dev/urandom");
    lib::crypto::ed25519_public_key(seed, pub);
    std::string base = argv[2];
    std::string k = to_hex(seed, 32) + "\n", p = to_hex(pub, 32) + "\n";
    write_file(base + ".key", k.data(), k.size());
    write_file(base + ".pub", p.data(), p.size());
    std::printf("public key: %s\nsecret key written to %s.key — keep it out of the repository\n", p.c_str(),
                base.c_str());
    return 0;
  }
  if (cmd == "pack" && argc >= 5) {
    auto m = read_file(argv[2]);
    std::string manifest(m.begin(), m.end());
    std::vector<uint8_t> seed;
    if (std::string(argv[3]) != "-") seed = from_hex_file(argv[3], 32);
    std::vector<sign::InFile> files;
    for (int i = 5; i < argc; ++i) {
      std::string spec = argv[i];
      bool exec = spec.size() > 2 && spec.compare(spec.size() - 2, 2, ":x") == 0;
      if (exec) spec.resize(spec.size() - 2);
      auto eq = spec.find('=');
      if (eq == std::string::npos) die("bad file spec " + spec);
      files.push_back({spec.substr(0, eq), read_file(spec.substr(eq + 1)), exec});
    }
    auto out = sign::pack(manifest, files, seed.empty() ? nullptr : seed.data());
    auto v = lib::package::open(out.data(), out.size());
    if (v.is_err()) die(std::string("package invalid: ") + lib::package::error_name(v.error()));
    write_file(argv[4], out.data(), out.size());
    std::printf("%s: %s %s, %zu files, %s\n", argv[4], v.value().manifest.id, v.value().manifest.version,
                files.size(), seed.empty() ? "unsigned" : "signed");
    return 0;
  }
  if (cmd == "verify" && (argc == 3 || argc == 4)) {
    auto data = read_file(argv[2]);
    auto v = lib::package::open(data.data(), data.size());
    if (v.is_err()) die(std::string("invalid: ") + lib::package::error_name(v.error()));
    std::vector<uint8_t> trusted;
    if (argc == 4) trusted = from_hex_file(argv[3], 32);
    auto s = lib::package::check_signature(v.value(), trusted.empty() ? nullptr : trusted.data());
    auto f = lib::package::check_files(v.value());
    std::printf("id=%s version=%s signature=%s files=%s\n", v.value().manifest.id, v.value().manifest.version,
                lib::package::error_name(s), lib::package::error_name(f));
    return s == lib::package::Error::Ok && f == lib::package::Error::Ok ? 0 : 1;
  }
  if (cmd == "pubkey-header" && argc == 4) {
    auto pub = from_hex_file(argv[2], 32);
    std::string h = "#pragma once\n// Открытый ключ магазина (tools/sign pubkey-header).\n#include <stdint.h>\n"
                    "inline constexpr uint8_t kStorePublicKey[32] = {";
    for (size_t i = 0; i < 32; ++i) {
      char b[8];
      std::snprintf(b, sizeof b, "%s0x%02x", i == 0 ? "" : ", ", pub[i]);
      h += b;
    }
    h += "};\n";
    write_file(argv[3], h.data(), h.size());
    return 0;
  }
  die("usage: sign keygen|pack|verify|pubkey-header ...");
}
