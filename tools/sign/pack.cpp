#include "pack.hpp"

#include <abi/package.hpp>
#include <lib/crypto/ed25519.hpp>
#include <lib/crypto/sha512.hpp>

#include <cstring>

namespace sign {

namespace pk = abi::package;

std::vector<uint8_t> pack(const std::string& manifest, const std::vector<InFile>& files, const uint8_t* seed) {
  auto align8 = [](uint64_t v) { return (v + 7) & ~uint64_t{7}; };
  pk::Header h{};
  std::memcpy(h.magic, pk::kMagic, 4);
  h.version = pk::kVersion;
  h.file_count = static_cast<uint32_t>(files.size());
  h.manifest_size = static_cast<uint32_t>(manifest.size());
  h.table_offset = sizeof h;
  h.manifest_offset = h.table_offset + files.size() * sizeof(pk::FileEntry);
  h.data_offset = align8(h.manifest_offset + manifest.size());
  h.signed_size = h.data_offset;
  std::vector<pk::FileEntry> table(files.size());
  uint64_t off = h.data_offset;
  for (size_t i = 0; i < files.size(); ++i) {
    pk::FileEntry& e = table[i];
    std::memset(&e, 0, sizeof e);
    std::strncpy(e.path, files[i].path.c_str(), pk::kMaxPath - 1);
    e.offset = off;
    e.size = files[i].data.size();
    e.mode = files[i].exec ? pk::kModeExec : 0;
    lib::crypto::sha512(files[i].data.data(), files[i].data.size(), e.sha512);
    off = align8(off + e.size);
  }
  h.signature_offset = seed != nullptr ? off : 0;
  std::vector<uint8_t> out(off + (seed != nullptr ? pk::kSignatureBlock : 0), 0);
  std::memcpy(out.data(), &h, sizeof h);
  std::memcpy(out.data() + h.table_offset, table.data(), table.size() * sizeof(pk::FileEntry));
  std::memcpy(out.data() + h.manifest_offset, manifest.data(), manifest.size());
  for (size_t i = 0; i < files.size(); ++i) {
    if (!files[i].data.empty()) std::memcpy(out.data() + table[i].offset, files[i].data.data(), files[i].data.size());
  }
  if (seed != nullptr) {
    uint8_t digest[64];
    lib::crypto::sha512(out.data(), h.signed_size, digest);
    lib::crypto::ed25519_sign(seed, digest, sizeof digest, out.data() + off);
    lib::crypto::ed25519_public_key(seed, out.data() + off + 64);
  }
  return out;
}

}  // namespace sign
