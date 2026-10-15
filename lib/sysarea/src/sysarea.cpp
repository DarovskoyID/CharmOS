#include <lib/crypto/sha512.hpp>
#include <lib/sysarea/sysarea.hpp>

namespace lib::sysarea {

using lib::fs::FsError;
using lib::fs::Status;

namespace {

void zero(void* p, size_t n) {
  auto* b = static_cast<volatile uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) b[i] = 0;
}

}  // namespace

void make_plain(Header* h, uint64_t partition_bytes) {
  zero(h, sizeof *h);
  for (int i = 0; i < 8; ++i) h->f.magic[i] = kMagic[i];
  h->f.version = kVersion;
  h->f.cipher = kCipherAes256Xts;
  h->f.state = static_cast<uint32_t>(State::Plain);
  h->f.data_offset = kDataOffset;
  h->f.data_units = partition_bytes > kDataOffset ? (partition_bytes - kDataOffset) / kUnit : 0;
  h->f.pcr_mask = kPcrMask;
  h->f.pcr_hash = kPcrSha256;
  seal_check(h);
}

void seal_check(Header* h) { lib::crypto::sha512(h, kCheckOffset, h->check); }

bool valid(const Header& h, uint64_t partition_bytes) {
  for (int i = 0; i < 8; ++i) {
    if (h.f.magic[i] != kMagic[i]) return false;
  }
  if (h.f.version != kVersion || h.f.cipher != kCipherAes256Xts) return false;
  uint8_t sum[64];
  lib::crypto::sha512(&h, kCheckOffset, sum);
  uint8_t diff = 0;
  for (int i = 0; i < 64; ++i) diff = static_cast<uint8_t>(diff | (sum[i] ^ h.check[i]));
  if (diff != 0) return false;
  if (h.f.state < 1 || h.f.state > 3) return false;
  if (h.f.data_offset != kDataOffset || h.f.data_units == 0) return false;
  if (h.f.data_units > (partition_bytes - kDataOffset) / kUnit) return false;
  if (h.f.progress > h.f.data_units) return false;
  return size_t{h.f.public_size} + h.f.private_size <= kSealedMax;
}

void XtsIo::init(lib::fs::Io* part, const lib::crypto::Xts256* key, uint64_t data_units, uint64_t limit) {
  part_ = part;
  key_ = key;
  units_ = data_units;
  limit_ = limit;
}

Status XtsIo::read_unit(uint64_t unit, uint8_t out[kUnit]) {
  if (auto s = part_->read(kDataOffset + unit * kUnit, out, kUnit); s.is_err()) return s;
  if (unit < limit_ && !key_->decrypt(unit, out, out, kUnit)) return lib::fs::fail(FsError::IoError);
  return lib::fs::ok();
}

Status XtsIo::write_unit(uint64_t unit, const uint8_t in[kUnit]) {
  uint8_t c[kUnit];
  const uint8_t* src = in;
  if (unit < limit_) {
    if (!key_->encrypt(unit, in, c, kUnit)) return lib::fs::fail(FsError::IoError);
    src = c;
  }
  Status s = part_->write(kDataOffset + unit * kUnit, src, kUnit);
  zero(c, sizeof c);
  return s;
}

Status XtsIo::read(uint64_t offset, void* buf, size_t len) {
  if (offset + len > size()) return lib::fs::fail(FsError::IoError);
  auto* out = static_cast<uint8_t*>(buf);
  uint8_t u[kUnit];
  while (len != 0) {
    uint64_t unit = offset / kUnit;
    size_t at = static_cast<size_t>(offset % kUnit);
    size_t n = kUnit - at < len ? kUnit - at : len;
    if (auto s = read_unit(unit, u); s.is_err()) return s;
    for (size_t i = 0; i < n; ++i) out[i] = u[at + i];
    out += n;
    offset += n;
    len -= n;
  }
  zero(u, sizeof u);
  return lib::fs::ok();
}

Status XtsIo::write(uint64_t offset, const void* buf, size_t len) {
  if (offset + len > size()) return lib::fs::fail(FsError::IoError);
  auto* in = static_cast<const uint8_t*>(buf);
  uint8_t u[kUnit];
  while (len != 0) {
    uint64_t unit = offset / kUnit;
    size_t at = static_cast<size_t>(offset % kUnit);
    size_t n = kUnit - at < len ? kUnit - at : len;
    if (n != kUnit) {
      if (auto s = read_unit(unit, u); s.is_err()) return s;
    }
    for (size_t i = 0; i < n; ++i) u[at + i] = in[i];
    if (auto s = write_unit(unit, u); s.is_err()) return s;
    in += n;
    offset += n;
    len -= n;
  }
  zero(u, sizeof u);
  return lib::fs::ok();
}

Status encrypt_units(lib::fs::Io* part, const lib::crypto::Xts256& key, uint64_t from, uint64_t to) {
  constexpr size_t kChunk = 64;  // единиц за одно чтение/запись (32 КиБ)
  static uint8_t buf[kChunk * kUnit];
  while (from < to) {
    size_t n = to - from < kChunk ? static_cast<size_t>(to - from) : kChunk;
    uint64_t off = kDataOffset + from * kUnit;
    if (auto s = part->read(off, buf, n * kUnit); s.is_err()) return s;
    for (size_t i = 0; i < n; ++i) {
      if (!key.encrypt(from + i, buf + i * kUnit, buf + i * kUnit, kUnit)) return lib::fs::fail(FsError::IoError);
    }
    if (auto s = part->write(off, buf, n * kUnit); s.is_err()) return s;
    from += n;
  }
  zero(buf, sizeof buf);
  return lib::fs::ok();
}

}  // namespace lib::sysarea
